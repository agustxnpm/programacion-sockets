#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "network.h"
#include "users.h"
#include "router.h"

#define PUERTO 9100

int main(void) {
    /*
     * Evita que write_all() hacia un cliente caído genere SIGPIPE,
     * cuya acción por defecto terminaría todo el proceso.
     * Con SIG_IGN, send() simplemente retorna -1 y el handler lo maneja.
     */
    signal(SIGPIPE, SIG_IGN);

    printf("Iniciando servidor de chat...\n");

    int server_fd = init_server(PUERTO);
    if (server_fd < 0) {
        perror("Error fatal al iniciar el servidor");
        exit(EXIT_FAILURE);
    }

    printf("Servidor escuchando en el puerto %d\n", PUERTO);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        /*
         * El FD se aloja en el heap para pasárselo al hilo sin que quede
         * en el stack de main (que podría pisar el valor en la próxima iteración).
         * client_handler() es responsable de liberar este puntero con free().
         */
        int *client_fd = malloc(sizeof(int));
        if (client_fd == NULL) {
            perror("malloc: sin memoria para el descriptor del cliente");
            continue;
        }

        *client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (*client_fd < 0) {
            free(client_fd);
            /* EINTR: accept() fue interrumpido por una señal; no es un error real. */
            if (errno == EINTR) {
                continue;
            }
            perror("Error al aceptar cliente");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        printf("[+] Nueva conexión desde %s:%d (fd=%d)\n",
               client_ip, ntohs(client_addr.sin_port), *client_fd);

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, client_handler, (void *)client_fd) != 0) {
            perror("Error al crear el hilo del cliente");
            close(*client_fd);
            free(client_fd);
            continue;
        }

        /* El hilo se autogestiona: al terminar, el SO libera su stack sin necesidad de join(). */
        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}
