#ifndef USERS_H
#define USERS_H

/*
 * CAPA DE ESTADO — Gestión de usuarios conectados con control de concurrencia.
 *
 * Este módulo mantiene la estructura global de usuarios activos.
 * TODA función de este módulo adquiere y libera internamente el pthread_mutex_t
 * antes de acceder o modificar la estructura compartida. El llamador NO debe
 * gestionar el mutex manualmente: hacerlo causaría deadlocks.
 *
 * Implementación: users.c
 */

/* Longitud fija reservada para nombres de usuario en el protocolo (ver OpCode 0x02–0x04) */
#define MAX_USERNAME_LEN 20

/*
 * Agrega un usuario a la estructura global asociándolo a su socket descriptor.
 * Adquiere el mutex internamente antes de modificar la lista.
 *
 * Retorna:
 *   1 — Éxito: el usuario fue registrado correctamente.
 *   0 — Fallo: ya existe un usuario con el mismo nombre (duplicado).
 *
 * Nota: 'name' debe ser un string terminado en '\0' de longitud <= MAX_USERNAME_LEN.
 */
int add_user(const char* name, int socket_fd);

/*
 * Elimina a un usuario de la estructura global utilizando su socket descriptor.
 * Adquiere el mutex internamente antes de modificar la lista.
 * Cierra el socket con close() de forma limpia antes de retornar.
 *
 * Llamar a esta función es obligatorio cuando read_all() retorne 0 (desconexión)
 * o -1 (error de socket) desde client_handler().
 */
void remove_user(int socket_fd);

/*
 * Busca el socket descriptor de un usuario por su nombre.
 * Adquiere el mutex internamente durante la búsqueda.
 *
 * Retorna el File Descriptor (FD) del socket si el usuario está conectado,
 * o -1 si no se encontró ningún usuario con ese nombre.
 *
 * Nota: 'name' no necesita ser de longitud exacta MAX_USERNAME_LEN; se compara
 * hasta el primer '\0'. El protocolo puede enviar bytes de relleno nulo que
 * la implementación debe ignorar al comparar.
 */
int get_socket_by_name(const char* name);

/*
 * Llena el array 'dest_fds' con los file descriptors de todos los usuarios conectados.
 * Guarda la cantidad total de usuarios activos en '*count'.
 * Adquiere el mutex internamente durante la copia.
 *
 * Uso principal: iterar sobre todos los sockets para mensajes de difusión (OpCode 0x06)
 * desde route_message(), excluyendo el socket de origen manualmente en el bucle.
 *
 * Precondición: 'dest_fds' debe apuntar a un array con capacidad suficiente
 * para el número máximo de clientes simultáneos definido en la implementación.
 */
void get_all_active_sockets(int* dest_fds, int* count);

#endif /* USERS_H */
