/* Importamos los módulos necesarios */
#include "router.h"
#include "users.h"
#include "network.h"
#include <string.h>    /* Para copiar y manipular memoria (memcpy, memset) */
#include <stdio.h>     /* Para funciones de texto formateado (snprintf) */
#include <stdlib.h>    /* Funciones estándar */
#include <arpa/inet.h> /* Para la función htonl() que formatea números para la red */
#include <pthread.h>   /* Para los candados (mutex) */
#include <sys/socket.h>/* Para apagar las conexiones (shutdown) */

/* Límite absoluto de conexiones concurrentes en la red */
#define MAX_FDS 1024 

/* Límite máximo de tamaño de archivo permitido: 100 MB (en bytes) */
#define MAX_FILE_SIZE (100 * 1024 * 1024)

/* Un lock por fd destino para evitar intercalado de bytes entre hilos
 * cuando múltiples emisores escriben al mismo socket simultáneamente. */
static pthread_mutex_t send_mutexes[MAX_FDS];
static pthread_once_t send_mutexes_once = PTHREAD_ONCE_INIT;

static void init_send_mutexes(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        pthread_mutex_init(&send_mutexes[i], NULL);
    }
}

/* ─────────────────────────────────────────────────────────────────────────────
 * Tabla de transferencias activas.
 * transfer_to_recv[sender_fd]   = receiver_fd  (-1 si no hay transferencia)
 * transfer_to_send[receiver_fd] = sender_fd    (-1 si no hay transferencia)
 * Protegida por transfer_mutex para acceso concurrente.
 * ───────────────────────────────────────────────────────────────────────────── */
static int transfer_to_recv[MAX_FDS];
static int transfer_to_send[MAX_FDS];
static pthread_mutex_t transfer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t  transfer_once  = PTHREAD_ONCE_INIT;

static void init_transfer_table(void) {
    for (int i = 0; i < MAX_FDS; i++) {
        transfer_to_recv[i] = -1;
        transfer_to_send[i] = -1;
    }
}

static void register_transfer(int sender_fd, int receiver_fd) {
    pthread_once(&transfer_once, init_transfer_table);
    if (sender_fd < 0 || sender_fd >= MAX_FDS) return;
    if (receiver_fd < 0 || receiver_fd >= MAX_FDS) return;
    pthread_mutex_lock(&transfer_mutex);
    transfer_to_recv[sender_fd]   = receiver_fd;
    transfer_to_send[receiver_fd] = sender_fd;
    pthread_mutex_unlock(&transfer_mutex);
}

static void clear_transfer(int sender_fd, int receiver_fd) {
    pthread_once(&transfer_once, init_transfer_table);
    pthread_mutex_lock(&transfer_mutex);
    if (sender_fd >= 0 && sender_fd < MAX_FDS)
        transfer_to_recv[sender_fd] = -1;
    if (receiver_fd >= 0 && receiver_fd < MAX_FDS)
        transfer_to_send[receiver_fd] = -1;
    pthread_mutex_unlock(&transfer_mutex);
}

/* Declaración anticipada — definición completa más abajo, antes de route_message */
static void send_error(int dest_socket, const char* msg);

/* Notifica al peer si fd tenía una transferencia activa y la limpia. */
void cancel_active_transfer(int fd) {
    pthread_once(&transfer_once, init_transfer_table);
    pthread_mutex_lock(&transfer_mutex);

    int peer_fd     = -1;
    int i_am_sender = 0;

    if (fd >= 0 && fd < MAX_FDS) {
        if (transfer_to_recv[fd] != -1) {
            /* fd es el emisor */
            peer_fd              = transfer_to_recv[fd];
            i_am_sender          = 1;
            transfer_to_recv[fd] = -1;
            if (peer_fd >= 0 && peer_fd < MAX_FDS)
                transfer_to_send[peer_fd] = -1;
        } else if (transfer_to_send[fd] != -1) {
            /* fd es el receptor */
            peer_fd              = transfer_to_send[fd];
            i_am_sender          = 0;
            transfer_to_send[fd] = -1;
            if (peer_fd >= 0 && peer_fd < MAX_FDS)
                transfer_to_recv[peer_fd] = -1;
        }
    }
    pthread_mutex_unlock(&transfer_mutex);

    if (peer_fd == -1) return;

    if (i_am_sender) {
        send_error(peer_fd,
            "Transferencia cancelada: el emisor se ha desconectado.");
    } else {
        send_error(peer_fd,
            "Transferencia cancelada: el receptor se ha desconectado.");
    }
}

static int send_packet_locked(int dest_socket, uint8_t opcode, const void* payload, uint32_t length) {
    uint32_t len_net = htonl(length);

    pthread_once(&send_mutexes_once, init_send_mutexes);
    if (dest_socket >= 0 && dest_socket < MAX_FDS) {
        pthread_mutex_lock(&send_mutexes[dest_socket]);
    }

    int ok = 1;
    if (write_all(dest_socket, &opcode, 1) < 0) ok = 0;
    if (ok && write_all(dest_socket, &len_net, 4) < 0) ok = 0;
    if (ok && length > 0 && write_all(dest_socket, payload, length) < 0) ok = 0;

    if (dest_socket >= 0 && dest_socket < MAX_FDS) {
        pthread_mutex_unlock(&send_mutexes[dest_socket]);
    }
    return ok;
}

/* Helper para enviar errores del protocolo a un origen de forma uniforme */
static void send_error(int dest_socket, const char* msg) {
    uint8_t err_sub = 0x01; /* 1 byte extra indicando sub-código de error general */
    uint32_t msg_len = (uint32_t)strlen(msg);
    uint32_t len = 1 + msg_len; /* sub-código + texto */
    unsigned char* payload = (unsigned char*)malloc(len);
    if (!payload) return;
    payload[0] = err_sub;
    memcpy(payload + 1, msg, msg_len);
    send_packet_locked(dest_socket, 0x05, payload, len);
    free(payload);
}

/* ─────────────────────────────────────────────
 * handle_disconnect
 * Llamado por client_handler al detectar desconexión.
 * Obtiene el nombre del usuario, lo elimina de la lista activa
 * (cerrando el socket) y difunde "X se ha desconectado" al resto.
 * ───────────────────────────────────────────── */
void handle_disconnect(int client_fd) {
    char username[MAX_USERNAME_LEN + 1] = {0};
    int has_name = get_name_by_socket(client_fd, username);

    remove_user(client_fd); /* cierra socket y lo elimina de la lista */

    if (has_name) {
        char msg[MAX_USERNAME_LEN + 32];
        snprintf(msg, sizeof(msg), "%s se ha desconectado", username);
        int fds[MAX_FDS];
        int count = 0;
        get_all_active_sockets(fds, &count);
        for (int i = 0; i < count; i++) {
            send_packet_locked(fds[i], 0x06, msg, (uint32_t)strlen(msg));
        }
    }
}

/* Esta es la gran función que decide qué hacer cuando recibimos un mensaje */
void route_message(int src_socket, unsigned char opcode, void* payload, uint32_t length) {
    /* El 'switch' toma decisiones basadas en el OpCode (el tipo de mensaje) */
    switch(opcode) { 
        case 0x01: { /* Login */
            char name[MAX_USERNAME_LEN + 1] = {0}; /* Prepara un espacio en blanco para el nombre */
            
            /* Evita un error si nos mandan un nombre súper largo: solo toma los caracteres permitidos */
            uint32_t name_len = length > MAX_USERNAME_LEN ? MAX_USERNAME_LEN : length;
            
            if (name_len == 0) {
                send_error(src_socket, "El nombre de usuario no puede estar vacio");
                shutdown(src_socket, SHUT_RDWR);
                break;
            }

            /* Copia el nombre extraído del contenido (payload) hacia nuestro espacio 'name' */
            memcpy(name, payload, name_len); 
            
            /* add_user trata de guardarlo. Si devuelve verdadero (1)... */
            if (add_user(name, src_socket)) {
                /* Notifica al origen de su login exitoso (0x07 subcódigo 0x03) */
                uint8_t sub = 0x03;          /* El sub-código 3 significa Login Exitoso */

                send_packet_locked(src_socket, 0x07, &sub, 1);
                
                /* Difunde ingreso al resto (0x06) */
                char msg[128]; /* Variable para crear el mensaje de texto */
                snprintf(msg, sizeof(msg), "%s se ha conectado", name); /* Forma la frase */
                
                uint8_t op_bcast = 0x06;                 /* OpCode de Difusión */
                
                int fds[MAX_FDS]; /* Arreglo para guardar IDs de todos los conectados */
                int count = 0;    /* Cuántos conectados hay */
                get_all_active_sockets(fds, &count); /* Llena la lista */
                
                /* Recorre a cada usuario activo para mandarle el aviso */
                for (int i = 0; i < count; i++) {
                    if (fds[i] != src_socket) { /* No le envía el aviso al que recién se conectó */
                        send_packet_locked(fds[i], op_bcast, msg, (uint32_t)strlen(msg));
                    }
                }

                /* Notifica al nuevo usuario quiénes ya estaban conectados (0x06 por cada uno) */
                char existing_names[MAX_FDS][MAX_USERNAME_LEN + 1];
                int existing_count = 0;
                get_all_active_names_except(src_socket, existing_names, &existing_count);
                for (int i = 0; i < existing_count; i++) {
                    char notice[128];
                    snprintf(notice, sizeof(notice), "%s se ha conectado", existing_names[i]);
                    send_packet_locked(src_socket, op_bcast, notice, (uint32_t)strlen(notice));
                }
            } else {
                /* Si add_user dio 0, el nombre ya existe o está lleno */
                send_error(src_socket, "Nombre duplicado o servidor lleno"); 
                /* Cierra forzosamente la conexión a nivel de sockets para que network.c reaccione */
                shutdown(src_socket, SHUT_RDWR);
            }
            break; /* Termina el caso de Login */
        }
        case 0x02: /* Mensaje Privado */
        case 0x03: /* Aviso de inicio de archivo */
        case 0x04: { /* Pedazo (Chunk) del archivo */
            /* Estos 3 casos comienzan buscando al destinatario, por lo que su código inicial es el mismo */
            if (length < MAX_USERNAME_LEN) return; /* Si el mensaje es más corto que un nombre, es inválido */
            
            char dest_name[MAX_USERNAME_LEN + 1] = {0}; /* Espacio para el nombre del que va a recibir */
            /* Copiamos los primeros 20 bytes del contenido (porque el destinatario siempre va al inicio) */
            memcpy(dest_name, payload, MAX_USERNAME_LEN); 
            
            /* Regla especial para Inicio de Archivo (0x03): Validar límite de 100MB */
            if (opcode == 0x03) {
                if (length < MAX_USERNAME_LEN + 8) return; /* Mensaje inválido, le faltan los 8 bytes del tamaño */

                uint64_t file_size = 0;
                unsigned char* size_ptr = (unsigned char*)payload + MAX_USERNAME_LEN;
                /* Extraemos los 8 bytes de tamaño asumiendo formato de red (Big-Endian) a un entero de C */
                for (int i = 0; i < 8; i++) {
                    file_size = (file_size << 8) | size_ptr[i];
                }
                if (file_size > MAX_FILE_SIZE) {
                    send_error(src_socket, "Archivo demasiado grande. El limite es 100 MB.");
                    return; /* Abortamos el ruteo de este mensaje */
                }
            }

            /* Regla especial para Chunk de Archivo (0x04): Validar tamaño máximo de chunk (64KB) */
            if (opcode == 0x04) {
                if (length > MAX_USERNAME_LEN + (64 * 1024)) {
                    send_error(src_socket, "Fragmento demasiado grande. El limite es 64 KB.");
                    
                    /* Avisar al receptor para que no se quede bloqueado esperando */
                    int dest_fd = get_socket_by_name(dest_name);
                    if (dest_fd != -1) {
                        send_error(dest_fd, "Transferencia cancelada: el emisor envió un fragmento inválido.");
                        clear_transfer(src_socket, dest_fd);
                    }
                    return; /* Abortamos el ruteo de este mensaje */
                }
            }
            
            /* Busca la conexión asociada a ese nombre */
            int dest_fd = get_socket_by_name(dest_name);
            if (dest_fd == -1) { /* Si no lo encontró, está desconectado o no existe */
                if (opcode == 0x04 || opcode == 0x03) {
                    /* Si es transferencia de archivo, usamos el error oficial documentado */
                    send_error(src_socket, "Transferencia abortada: el destinatario no está conectado.");
                } else {
                    /* Si es un simple chat privado... */
                    send_error(src_socket, "El usuario no existe o no está conectado.");
                }
                return; /* No hacemos nada más y salimos de la función */
            }
            
            /* En transferencia de archivos, reescribimos el campo inicial (20 bytes)
             * para que el receptor vea el nombre del EMISOR. Así cada ACK puede indicar
             * a qué emisor corresponde y el servidor enruta correctamente concurrencia. */
            if (opcode == 0x03 || opcode == 0x04) {
                char src_name[MAX_USERNAME_LEN + 1] = {0};
                if (!get_name_by_socket(src_socket, src_name)) {
                    send_error(src_socket, "Error interno: no se pudo identificar el emisor");
                    return;
                }

                unsigned char* forward_payload = (unsigned char*)malloc(length);
                if (!forward_payload) {
                    send_error(src_socket, "Error interno: memoria insuficiente");
                    return;
                }

                memcpy(forward_payload, payload, length);
                memset(forward_payload, 0, MAX_USERNAME_LEN);
                memcpy(forward_payload, src_name, strnlen(src_name, MAX_USERNAME_LEN));

                uint32_t len_net = htonl(length);
                (void)len_net;
                send_packet_locked(dest_fd, opcode, forward_payload, length);
                free(forward_payload);

                /* Registrar transferencia activa al enviar el aviso inicial (0x03) */
                if (opcode == 0x03) {
                    register_transfer(src_socket, dest_fd);
                }
                break;
            }

            /* Para privados (0x02) se reenvía sin cambios. */
            send_packet_locked(dest_fd, opcode, payload, length);
            break;
        }
        case 0x05: { /* Error de origen interno. No se rutea bajo el protocolo. */
            /* Si el cliente nos manda un error (ej. botón de cancelar), 
             * limpiamos su transferencia activa y avisamos al peer. */
            pthread_once(&transfer_once, init_transfer_table);
            pthread_mutex_lock(&transfer_mutex);

            int peer_fd = -1;
            int i_am_sender = 0;

            if (src_socket >= 0 && src_socket < MAX_FDS) {
                if (transfer_to_recv[src_socket] != -1) {
                    peer_fd = transfer_to_recv[src_socket];
                    i_am_sender = 1;
                    transfer_to_recv[src_socket] = -1;
                    if (peer_fd >= 0 && peer_fd < MAX_FDS)
                        transfer_to_send[peer_fd] = -1;
                } else if (transfer_to_send[src_socket] != -1) {
                    peer_fd = transfer_to_send[src_socket];
                    i_am_sender = 0;
                    transfer_to_send[src_socket] = -1;
                    if (peer_fd >= 0 && peer_fd < MAX_FDS)
                        transfer_to_recv[peer_fd] = -1;
                }
            }
            pthread_mutex_unlock(&transfer_mutex);

            if (peer_fd != -1) {
                if (i_am_sender) {
                    send_error(peer_fd, "Transferencia cancelada: el emisor cancelo el envio.");
                } else {
                    send_error(peer_fd, "Transferencia cancelada: el receptor cancelo la descarga.");
                }
            }
            break;
        }
        case 0x06: { /* Difusión global */
            int fds[MAX_FDS];
            int count = 0;
            get_all_active_sockets(fds, &count); /* Obtenemos los IDs de todos */
            
            /* Reenvía el mensaje textual a todos */
            for (int i = 0; i < count; i++) {
                if (fds[i] != src_socket) { /* Salvo al que lo mandó */
                    send_packet_locked(fds[i], opcode, payload, length);
                }
            }
            break;
        }
        case 0x07: { /* ACK Polimórfico */
            if (length < 1) return; /* Un ACK debe tener mínimo 1 byte */
            
            /* Leemos el primer byte del contenido para saber si es 0x01 o 0x02 */
            uint8_t subcode = ((uint8_t*)payload)[0]; 
            
            if (subcode == 0x01 || subcode == 0x02) { /* Son ACKs de transferencia de archivos */
                /* Nuevo formato esperado:
                 * 0x01: [subcode][sender(20)]
                 * 0x02: [subcode][flag][sender(20)]
                 */
                int min_len = (subcode == 0x01) ? (1 + MAX_USERNAME_LEN) : (1 + 1 + MAX_USERNAME_LEN);
                if ((int)length < min_len) return;

                const unsigned char* sender_field = (subcode == 0x01)
                    ? ((unsigned char*)payload + 1)
                    : ((unsigned char*)payload + 2);

                char sender_name[MAX_USERNAME_LEN + 1] = {0};
                memcpy(sender_name, sender_field, MAX_USERNAME_LEN);
                int sender_fd = get_socket_by_name(sender_name);

                if (sender_fd != -1) {
                    send_packet_locked(sender_fd, opcode, payload, length);
                }

                /* Si es rechazo de consentimiento (0x07/0x02/0x00): limpiar transferencia */
                if (subcode == 0x02 && length >= 2) {
                    uint8_t flag = ((uint8_t*)payload)[1];
                    if (flag == 0x00) {
                        clear_transfer(
                            (sender_fd >= 0 && sender_fd < MAX_FDS) ? sender_fd : -1,
                            src_socket
                        );
                    }
                }
            }
            break;
        }
    }
}