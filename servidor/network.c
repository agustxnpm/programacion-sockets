#include "network.h"
#include "router.h"
#include "users.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* ─────────────────────────────────────────────
 * init_server
 * Crea el socket pasivo del servidor.
 * Retorna el FD del socket o -1 en caso de error.
 * ───────────────────────────────────────────── */
int init_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }

    /* Permite reusar el puerto inmediatamente después de cerrar el servidor */
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; //aceptar conexiones en cualquier interfaz de red
    addr.sin_port        = htons(port); //convierte el puerto a formato de red

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    printf("[server] Escuchando en puerto %d\n", port);
    return server_fd;
}

/* ─────────────────────────────────────────────
 * read_all
 * Lee exactamente 'size' bytes del socket.
 * Usa recv() en bucle para mitigar Short Counts.
 *
 * Retorna:
 *   1  — Lectura completa.
 *   0  — Cliente cerró la conexión.
 *  -1  — Error de socket.
 * ───────────────────────────────────────────── */
int read_all(int socket_fd, void* buffer, size_t size) {
    size_t total   = 0;
    char*  buf_ptr = (char*)buffer;

    while (total < size) {
        ssize_t bytes = recv(socket_fd, buf_ptr + total, size - total, 0);

        if (bytes == 0) {
            /* El cliente cerró la conexión limpiamente (FIN) */
            return 0;
        }
        if (bytes < 0) {
            /* Error real o timeout (EAGAIN / EWOULDBLOCK si SO_RCVTIMEO expiró) */
            return -1;
        }

        total += (size_t)bytes;
    }

    return 1;
}

/* ─────────────────────────────────────────────
 * write_all
 * Escribe exactamente 'size' bytes al socket.
 * Usa send() en bucle para mitigar Short Counts.
 *
 * Retorna:
 *   1  — Escritura completa.
 *  -1  — Error de socket.
 * ───────────────────────────────────────────── */
int write_all(int socket_fd, const void* buffer, size_t size) {
    size_t      total   = 0;
    const char* buf_ptr = (const char*)buffer;

    while (total < size) {
        ssize_t bytes = send(socket_fd, buf_ptr + total, size - total, 0);

        if (bytes < 0) {
            return -1;
        }

        total += (size_t)bytes;
    }

    return 1;
}

/* ─────────────────────────────────────────────
 * client_handler
 * Rutina pthread por cliente.
 *
 * Ciclo de vida:
 *   1. Configura SO_RCVTIMEO (10 s) — primera instrucción.
 *   2. Lee 5 bytes de cabecera (1 OpCode + 4 Payload Length).
 *   3. Convierte Payload Length de Network a Host byte order.
 *   4. Reserva buffer y lee el payload completo.
 *   5. Delega a route_message().
 *   6. Libera buffer y repite.
 *   Al salir: cierra socket y llama a remove_user().
 * ───────────────────────────────────────────── */
void* client_handler(void* arg) {
    int client_fd = *((int*)arg);
    free(arg);  /* El FD fue alojado en heap por el aceptador */

    /* ── 1. Timeout de recepción: 10 segundos ─────────────────────────── */
    struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    /* ── Cabecera: 1 byte OpCode + 4 bytes Payload Length ─────────────── */
    /*    Total = 5 bytes fijos por mensaje                                 */
    #define HEADER_SIZE 5

    while (1) {
        unsigned char header[HEADER_SIZE];

        /* ── 2. Leer cabecera completa ─────────────────────────────────── */
        int result = read_all(client_fd, header, HEADER_SIZE);

        if (result == 0) {
            /* Desconexión limpia */
            printf("[handler fd=%d] Cliente desconectado (FIN).\n", client_fd);
            break;
        }
        if (result == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("[handler fd=%d] Timeout: cliente fantasma. Cerrando.\n", client_fd);
            } else {
                perror("[handler] read_all cabecera");
            }
            break;
        }

        /* ── 3. Desempaquetar cabecera ─────────────────────────────────── */
        unsigned char opcode = header[0];

        uint32_t payload_len_net;
        memcpy(&payload_len_net, header + 1, sizeof(uint32_t));
        uint32_t payload_len = ntohl(payload_len_net);  /* Network → Host byte order */

        /* ── 4. Reservar buffer y leer payload ────────────────────────── */
        void* payload = NULL;

        if (payload_len > 0) {
            payload = malloc(payload_len);
            if (!payload) {
                fprintf(stderr, "[handler fd=%d] malloc falló para payload de %u bytes.\n",
                        client_fd, payload_len);
                break;
            }

            result = read_all(client_fd, payload, payload_len);
            if (result == 0) {
                printf("[handler fd=%d] Cliente desconectado leyendo payload.\n", client_fd);
                free(payload);
                break;
            }
            if (result == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    printf("[handler fd=%d] Timeout leyendo payload. Cerrando.\n", client_fd);
                } else {
                    perror("[handler] read_all payload");
                }
                free(payload);
                break;
            }
        }

        /* ── 5. Delegar a la capa de aplicación ───────────────────────── */
        route_message(client_fd, opcode, payload, payload_len);

        /* ── 6. Liberar buffer y continuar ciclo ──────────────────────── */
        free(payload);
    }

    /* ── Limpieza al salir del ciclo ──────────────────────────────────── */
    remove_user(client_fd);  /* Cierra el socket y elimina de la lista activa */

    return NULL;
}