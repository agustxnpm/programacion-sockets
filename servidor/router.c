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

/* Esta es la gran función que decide qué hacer cuando recibimos un mensaje */
void route_message(int src_socket, unsigned char opcode, void* payload, uint32_t length) {
    /* El 'switch' toma decisiones basadas en el OpCode (el tipo de mensaje) */
    switch(opcode) { 
        case 0x01: { /* Login */
            char name[MAX_USERNAME_LEN + 1] = {0}; /* Prepara un espacio en blanco para el nombre */
            
            /* Evita un error si nos mandan un nombre súper largo: solo toma los caracteres permitidos */
            uint32_t name_len = length > MAX_USERNAME_LEN ? MAX_USERNAME_LEN : length;
            
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
            
            /* Busca la conexión asociada a ese nombre */
            int dest_fd = get_socket_by_name(dest_name);
            if (dest_fd == -1) { /* Si no lo encontró, está desconectado o no existe */
                if (opcode == 0x04 || opcode == 0x03) {
                    /* Si es transferencia de archivo, usamos el error oficial documentado */
                    send_error(src_socket, "Transferencia abortada: El destinatario se ha desconectado");
                } else {
                    /* Si es un simple chat privado... */
                    send_error(src_socket, "Usuario no existe");
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
                break;
            }

            /* Para privados (0x02) se reenvía sin cambios. */
            send_packet_locked(dest_fd, opcode, payload, length);
            break;
        }
        case 0x05: { /* Error de origen interno. No se rutea bajo el protocolo. */
            /* El cliente nunca debería enviarnos 0x05. Si lo hace, lo ignoramos. */
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
            }
            break;
        }
    }
}