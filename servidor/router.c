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

/* Mecanismo para rastrear el emisor original durante transferencias y redirigir los ACKs */
/* Esto asocia a quién le estamos mandando el archivo para devolverle luego el ACK */
static int transfer_partner[MAX_FDS];
static pthread_mutex_t partner_mutex = PTHREAD_MUTEX_INITIALIZER; /* Candado para este arreglo */

/* Función interna para iniciar el arreglo la primera vez con puros -1 (vacío) */
static void init_partners() {
    static int initialized = 0;
    if (!initialized) {
        for(int i = 0; i < MAX_FDS; i++) transfer_partner[i] = -1;
        initialized = 1; /* Marca que ya fue inicializado */
    }
}

/* Función para guardar quién le está enviando un archivo a quién */
static void set_transfer_partner(int receiver_fd, int sender_fd) {
    if (receiver_fd >= 0 && receiver_fd < MAX_FDS) {
        pthread_mutex_lock(&partner_mutex); /* Candado puesto */
        init_partners(); /* Asegura que la lista esté inicializada */
        transfer_partner[receiver_fd] = sender_fd; /* Anota: "El receptor está conectado con este emisor" */
        pthread_mutex_unlock(&partner_mutex); /* Candado quitado */
    }
}

/* Función para recuperar el emisor original asociado a un receptor (cuando el receptor envía el ACK) */
static int get_transfer_partner(int receiver_fd) {
    int sender_fd = -1;
    if (receiver_fd >= 0 && receiver_fd < MAX_FDS) {
        pthread_mutex_lock(&partner_mutex); /* Candado puesto */
        init_partners();
        sender_fd = transfer_partner[receiver_fd]; /* Recupera la información */
        pthread_mutex_unlock(&partner_mutex); /* Candado quitado */
    }
    return sender_fd; /* Devuelve el ID de conexión del emisor original */
}

/* Helper para enviar errores del protocolo a un origen de forma uniforme */
static void send_error(int dest_socket, const char* msg) {
    uint8_t op = 0x05; /* Código de operación 0x05 es Error */
    uint8_t err_sub = 0x01; /* 1 byte extra indicando sub-código de error general */
    uint32_t len = 1 + strlen(msg); /* Tamaño total: 1 del sub-código + la cantidad de letras del mensaje */
    uint32_t len_net = htonl(len);  /* Transforma el tamaño a formato de red para que otras PC lo entiendan */
    
    /* write_all es la función en network.c que envía bytes al cliente */
    write_all(dest_socket, &op, 1);          /* Enviar OpCode (1 byte) */
    write_all(dest_socket, &len_net, 4);     /* Enviar tamaño (4 bytes) */
    write_all(dest_socket, &err_sub, 1);     /* Enviar el sub-código (1 byte) */
    write_all(dest_socket, msg, strlen(msg));/* Enviar el texto del error */
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
                uint8_t op = 0x07;           /* OpCode de confirmación */
                uint32_t len_net = htonl(1); /* Solo tendrá 1 byte de contenido (el sub-código) */
                uint8_t sub = 0x03;          /* El sub-código 3 significa Login Exitoso */
                
                write_all(src_socket, &op, 1);       /* Mandar OpCode */
                write_all(src_socket, &len_net, 4);  /* Mandar tamaño */
                write_all(src_socket, &sub, 1);      /* Mandar contenido (sub-código 0x03) */
                
                /* Difunde ingreso al resto (0x06) */
                char msg[128]; /* Variable para crear el mensaje de texto */
                snprintf(msg, sizeof(msg), "%s se ha conectado", name); /* Forma la frase */
                
                uint8_t op_bcast = 0x06;                 /* OpCode de Difusión */
                uint32_t bcast_len = htonl(strlen(msg)); /* Calcula su tamaño de red */
                
                int fds[MAX_FDS]; /* Arreglo para guardar IDs de todos los conectados */
                int count = 0;    /* Cuántos conectados hay */
                get_all_active_sockets(fds, &count); /* Llena la lista */
                
                /* Recorre a cada usuario activo para mandarle el aviso */
                for (int i = 0; i < count; i++) {
                    if (fds[i] != src_socket) { /* No le envía el aviso al que recién se conectó */
                        write_all(fds[i], &op_bcast, 1);
                        write_all(fds[i], &bcast_len, 4);
                        write_all(fds[i], msg, strlen(msg));
                    }
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
            
            /* Actualiza el mapeo de transferencias para encaminar correctamente el ACK */
            /* Si están pasándose archivos, anotamos quién es el emisor para el receptor encontrado */
            if (opcode == 0x03 || opcode == 0x04) {
                set_transfer_partner(dest_fd, src_socket);
            }
            
            /* Como el destinatario existe, simplemente reenviamos todo el paquete como nos llegó */
            uint32_t len_net = htonl(length);    /* Tamaño completo en formato red */
            write_all(dest_fd, &opcode, 1);      /* Opcode original */
            write_all(dest_fd, &len_net, 4);     /* Tamaño original */
            write_all(dest_fd, payload, length); /* Reenvía toda la "carta" */
            break;
        }
        case 0x05: { /* Error de origen interno. No se rutea bajo el protocolo. */
            /* El cliente nunca debería enviarnos 0x05. Si lo hace, lo ignoramos. */
            break;
        }
        case 0x06: { /* Difusión global */
            uint32_t len_net = htonl(length); /* Formato red */
            int fds[MAX_FDS];
            int count = 0;
            get_all_active_sockets(fds, &count); /* Obtenemos los IDs de todos */
            
            /* Reenvía el mensaje textual a todos */
            for (int i = 0; i < count; i++) {
                if (fds[i] != src_socket) { /* Salvo al que lo mandó */
                    write_all(fds[i], &opcode, 1);
                    write_all(fds[i], &len_net, 4);
                    write_all(fds[i], payload, length);
                }
            }
            break;
        }
        case 0x07: { /* ACK Polimórfico */
            if (length < 1) return; /* Un ACK debe tener mínimo 1 byte */
            
            /* Leemos el primer byte del contenido para saber si es 0x01 o 0x02 */
            uint8_t subcode = ((uint8_t*)payload)[0]; 
            
            if (subcode == 0x01 || subcode == 0x02) { /* Son ACKs de transferencia de archivos */
                /* Preguntamos a nuestra libreta "transfer_partner": ¿quién le estaba enviando un archivo a este usuario? */
                int sender_fd = get_transfer_partner(src_socket);
                
                if (sender_fd != -1) { /* Si hay un emisor esperando la confirmación... */
                    uint32_t len_net = htonl(length);
                    write_all(sender_fd, &opcode, 1);      /* Le rebotamos el ACK completo al emisor */
                    write_all(sender_fd, &len_net, 4);
                    write_all(sender_fd, payload, length); 
                }
            }
            break;
        }
    }
}