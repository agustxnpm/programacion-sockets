#ifndef ROUTER_H
#define ROUTER_H

#include <stdint.h>  /* uint32_t */

/*
 * CAPA DE APLICACIÓN — Enrutamiento de mensajes según el protocolo de chat.
 *
 * Este módulo es el punto de unión entre la infraestructura de red (network.c)
 * y la gestión de estado (users.c). Recibe paquetes ya deserializados y
 * aplica la lógica de negocio según el OpCode.
 *
 * Dependencias:
 *   - write_all()          (network.h) — para reenviar mensajes y enviar errores.
 *   - get_socket_by_name() (users.h)   — para resolver destinatarios por nombre.
 *   - get_all_active_sockets() (users.h) — para mensajes de difusión (0x06).
 *   - add_user() / remove_user() (users.h) — para login y desconexión.
 *
 * Implementación: router.c
 */

/*
 * Evalúa el OpCode y ejecuta el enrutamiento correspondiente.
 *
 * Parámetros:
 *   src_socket — File descriptor del cliente que originó el mensaje.
 *   opcode     — Código de operación extraído del primer byte de la cabecera.
 *   payload    — Puntero al buffer con el contenido del mensaje (ya leído).
 *   length     — Cantidad de bytes válidos en 'payload' (Payload Length del header).
 *
 * Comportamiento por OpCode:
 *   0x01 (Login)         — Extrae el nombre del payload y llama a add_user().
 *                          Si add_user() retorna 0 (duplicado), envía error 0x05.
 *   0x02 (Privado)       — Lee los primeros MAX_USERNAME_LEN bytes del payload
 *                          como nombre de destinatario. Llama a get_socket_by_name();
 *                          si retorna -1, envía error 0x05 (ver abajo). Si existe,
 *                          reenvía el paquete completo al FD destino con write_all().
 *   0x03 (Inicio Archivo)— Igual que 0x02: valida destinatario y reenvía el aviso.
 *   0x04 (Chunk Archivo) — Igual que 0x02: valida destinatario y reenvía el chunk.
 *   0x05 (Error)         — Reservado para uso interno del servidor. No se enruta.
 *   0x06 (Difusión)      — Llama a get_all_active_sockets() y reenvía el paquete
 *                          a todos los FDs activos, excluyendo src_socket.
 *   0x07 (ACK Archivo)   — Reenvía la confirmación al emisor original del archivo.
 *                          El servidor debe rastrear quién inició la transferencia.
 *
 * Manejo de error por destinatario inexistente (OpCodes 0x02, 0x03, 0x04):
 *   Si get_socket_by_name() retorna -1, esta función DEBE llamar inmediatamente
 *   a write_all() enviando un paquete con OpCode 0x05 hacia src_socket antes de
 *   retornar. No propaga el error mediante valor de retorno (la función es void).
 */
void route_message(int src_socket, unsigned char opcode, void* payload, uint32_t length);

#endif /* ROUTER_H */
