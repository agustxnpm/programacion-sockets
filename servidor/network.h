#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>  /* size_t */

/*
 * CAPA DE RED — Infraestructura pura de sockets e hilos.
 *
 * Este módulo NO conoce nombres de usuarios ni lógica de negocio.
 * Solo transporta bytes entre descriptores de socket.
 * Implementación: network.c
 */

/*
 * Inicializa el socket pasivo del servidor en el puerto especificado.
 * Internamente llama a socket(), bind() y listen().
 * Retorna el descriptor del socket del servidor (>= 0) o -1 en caso de error.
 */
int init_server(int port);


/*
 * Short Count (Transferencia Parcial): 
 * Ocurre cuando recv() o send() procesan MENOS bytes de los solicitados 
 * debido al estado de los buffers del sistema operativo (TCP es un flujo continuo). 
 * Este bucle while asegura reintentar la llamada hasta transferir el tamaño EXACTO.
 */

/*
 * Asegura la lectura COMPLETA de exactamente 'size' bytes del socket (mitiga Short Counts).
 *
 * Internamente llama a recv() en un bucle hasta completar la lectura.
 * NO usar read() directamente: recv() es la primitiva correcta para sockets TCP.
 *
 * Retorna:
 *   1  — Lectura completada con éxito.
 *   0  — El cliente cerró la conexión (recv retornó 0 bytes).
 *  -1  — Error de socket (errno tendrá el detalle).
 */
int read_all(int socket_fd, void* buffer, size_t size);

/*
 * Asegura la escritura COMPLETA de exactamente 'size' bytes al socket (mitiga Short Counts).
 *
 * Internamente llama a send() en un bucle hasta completar la escritura.
 * NO usar write() directamente: send() es la primitiva correcta para sockets TCP.
 *
 * Retorna:
 *   1  — Escritura completada con éxito.
 *  -1  — Error de socket (errno tendrá el detalle).
 */
int write_all(int socket_fd, const void* buffer, size_t size);

/*
 * Rutina principal que ejecutará cada hilo dedicado a un cliente (pthread).
 * El argumento 'arg' es un puntero a int que contiene el file descriptor del socket del cliente.
 *
 * Ciclo de vida del hilo:
 *   1. Leer los 5 bytes de cabecera con read_all().
 *   2. Desempaquetar el Payload Length y convertirlo a host byte order con ntohl().
 *   3. Reservar un buffer temporal y leer el payload con read_all().
 *   4. Delegar a route_message(client_fd, opcode, payload, length).
 *   5. Liberar el buffer y repetir hasta que read_all() indique desconexión o error.
 *
 * Al salir del bucle: cierra el socket y llama a remove_user() para liberar el estado.
 * Este handler NO contiene switch-case; toda la lógica de OpCodes vive en router.c.
 */
void* client_handler(void* arg);

#endif /* NETWORK_H */
