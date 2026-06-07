# Plan de Desarrollo: Trabajo Práctico 4
# Programación de Sockets y Protocolos de Comunicación

Este documento establece la hoja de ruta técnica para la implementación del sistema de chat y transferencia de archivos. Se divide en cinco fases críticas que deben ejecutarse de manera secuencial para asegurar la integridad de la arquitectura.

**Fecha de Inicio:** 4 jun 2026  

---

## Fase 1: Especificación del Protocolo (El Contrato Base)

Esta fase define las reglas estáticas de comunicación.

### Tarea 1.1: Definición de la Cabecera (Header) Estándar

- **Justificación Técnica:** Dado que TCP es un protocolo orientado a flujos (stream), es imperativo delimitar los mensajes en el buffer para evitar el solapamiento de datos.
- **Entrada:** Consenso técnico del equipo.
- **Salida:** Especificación de envío obligatorio de 5 bytes iniciales:
  - **OpCode:** 1 byte (Código de operación).
  - **Payload Length:** 4 bytes (Entero sin signo, Big-Endian).

### Tarea 1.2: Tabla de Operaciones y Estructura de Payloads

A continuación se detalla el mapeo de códigos y la estructura de sus respectivos datos:

| OpCode | Operación     | Estructura del Payload                                          |
|--------|---------------|-----------------------------------------------------------------|
| 0x01   | Login         | [Nombre de Usuario (String dinámico)]                           |
| 0x02   | Privado       | [Destinatario (20 bytes)] [Mensaje (String dinámico)]           |
| 0x03   | Inicio Archivo| [Destinatario (20 bytes)] [Nombre (String)] [Tamaño (8 bytes)] |
| 0x04   | Chunk Archivo | [Destinatario (20 bytes)] [Bytes del Chunk (máx. 4096)]        |
| 0x05   | Error         | [Código (1 byte)] [Mensaje (String dinámico)]                   |
| 0x06   | Difusión      | [Mensaje (String dinámico)]                                     |
| 0x07   | ACK Polimórfico | [Sub-Código (1 byte)] + [Datos del subtipo]: `0x01` = ACK de Fragmento (1 byte total); `0x02` + `0x01`/`0x00` = ACK de Consentimiento (aceptar/rechazar); `0x03` = ACK de Login Exitoso. Reemplaza al antiguo `0x08`. |

### Estructura de Archivos Recomendada (Servidor C)

```
/servidor
├── main.c      (Punto de entrada, inicializa todo)
├── network.c   (Código de sockets e hilos)           - fase 2
├── network.h   (Contrato de funciones de red)         - fase 2
├── users.c     (Código de la lista de usuarios y Mutex) - fase 3
├── users.h     (Contrato de funciones de estado)      - fase 3
├── router.c    (Código del switch-case y enrutamiento) - fase 3
└── router.h    (Contrato de funciones de lógica)      - fase 3
```

---

## Fase 2: Infraestructura del Servidor C (Capa de Red) — Sockets e Hilos

Módulo encargado exclusivamente de la conectividad y el manejo de flujos de bytes crudos.

### Tarea 2.0: Inicialización del Servidor (`init_server`)

- **Especificación:** Implementación de la función `int init_server(int port);` que crea el socket, realiza el `bind` y el `listen`, retornando el descriptor del socket o `-1`.

### Tarea 2.1: Wrappers de lectura y escritura segura

- **Justificación:** Mitigación de *Short Counts* en las funciones `recv()` y `send()` debido a saturación de buffers.
- **Especificación:** Implementación de las siguientes firmas exactas:

```c
int read_all(int socket_fd, void* buffer, size_t size);
int write_all(int socket_fd, const void* buffer, size_t size);
```

- **Retorno de `read_all`:** `1` si leyó todo con éxito, `0` si el cliente se desconectó, o `-1` si hubo un error.

### Tarea 2.2: Bucle de Aceptación y Despliegue de Hilos

- **Justificación:** El hilo principal debe permanecer libre para llamadas a `accept()`, evitando el bloqueo por procesamiento de clientes.
- **Salida:** Instanciación de un hilo `pthread` por cada conexión, pasando únicamente el File Descriptor (FD) como argumento.

### Tarea 2.3: Controlador del Ciclo de Vida del Hilo (Thread Handler)

- **Justificación:** Gestión de la rutina dedicada por cliente hasta su desconexión.
- **Proceso:** Implementación de `void* client_handler(void* arg);`, que recibe el descriptor del socket del cliente como un puntero (`int*`). El handler **no implementa el `switch-case`**; su única responsabilidad es extraer los datos crudos y delegar. El orden secuencial de operaciones es:

  0. **Configuración de Timeout (primera instrucción del handler):** Antes de entrar al ciclo de lectura, configurar el timeout de recepción del socket:
     ```c
     struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
     setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
     ```
     Si el socket no recibe ningún byte durante **10 segundos consecutivos**, la próxima llamada a `read_all` retornará `-1` con `errno == EAGAIN` o `EWOULDBLOCK`.
  1. Llama a `read_all` para obtener los 5 bytes de la cabecera.
  2. Desempaqueta el `Payload Length` y convierte el entero de Network Byte Order a Host Byte Order usando `ntohl()`.
  3. Reserva un buffer temporal y llama a `read_all` para obtener los bytes del payload.
  4. Invoca a `route_message(client_fd, opcode, payload, length)`.
  5. Libera el buffer temporal y repite el ciclo.

- **Manejo de fallos (incluye timeout):** Si `read_all` retorna `0` (desconexión limpia), `-1` por error de red, o `-1` por expiración del `SO_RCVTIMEO` (cliente fantasma), se procede al cierre forzado del socket y a la llamada inmediata a `remove_user(client_fd)`.

---

## Fase 3: Lógica Central del Servidor C (Capa de Aplicación) — Usuarios y Enrutamiento

Este módulo interpreta las reglas de negocio y coordina la distribución de la información.

### Tarea 3.1: Gestor de Estado y Concurrencia

- **Justificación:** Prevención de *Race Conditions* mediante el uso de Mutex al acceder a la lista de usuarios activos.
- **Operaciones requeridas** (asegurando mutex):
  - `int add_user(const char* name, int socket_fd);`: Agrega usuario. Retorna `1` (éxito) o `0` (nombre duplicado).
  - `void remove_user(int socket_fd);`: Elimina al usuario de la lista y cierra el socket limpiamente.
  - `int get_socket_by_name(const char* name);`: Busca el descriptor del socket. Retorna FD o `-1` (no encontrado).

### Tarea 3.2: Enrutador de Mensajes (Switch-Case Principal)

- **Justificación:** Núcleo lógico que decide el flujo según el OpCode recibido.
- **Función de Entrada:** `void route_message(int src_socket, unsigned char opcode, void* payload, uint32_t length);`
- **Comportamientos Clave:**
  - **Login (`0x01`):** Invoca `add_user`. Si tiene éxito, envía `0x07` con sub-código `0x03` (ACK de Login Exitoso, payload de 1 byte: `[0x03]`) directamente al `src_socket`; inmediatamente después, itera sobre todos los demás descriptores activos enviando un `0x06` (Difusión) notificando el ingreso. Si el nombre está duplicado, envía `0x05` y cierra la conexión.
  - **Mensajería/Archivos:** Validación de destinatario; envío de error (`0x05`) si el usuario no existe.
  - **Difusión:** Iteración sobre descriptores activos (excluyendo origen) bajo bloqueo de lista.
- **Clarificación:** Si `get_socket_by_name` retorna `-1`, la función `route_message` debe invocar inmediatamente a `write_all` hacia el `src_socket` enviando el OpCode `0x05` antes de finalizar su ejecución.

---

## Fase 4: Desarrollo del Cliente

Módulo de interacción con el usuario, desarrollado de forma aislada respetando la Fase 1.

### Tarea 4.1: Módulo de Serialización (Capa de Red)

- **Justificación:** Conversión de objetos de alto nivel a bytes estructurados mediante `struct.pack()`, respetando el endianness acordado.

### Tarea 4.2: Hilo de Escucha Asíncrono

- **Justificación:** Evitar el congelamiento de la GUI durante llamadas bloqueantes de red.
- **Salida:** Hilo secundario que procesa cabeceras y payloads, emitiendo callbacks a la interfaz principal para actualizar la vista. Debe manejar explícitamente el evento `0x07` con sub-código `0x03` (ACK de Login Exitoso) para habilitar los controles de la interfaz únicamente tras recibir la confirmación del servidor, sin asumir éxito por ausencia de error.

### Tarea 4.3: Controlador de Flujo de Archivos (Chunking)

- **Justificación:** Control de congestión del buffer TCP e implementación del *handshake* de consentimiento para no saturar el sistema de archivos del receptor.
- **Lógica (flujo Stop-and-Wait con handshake):**
  1. Enviar `0x03` (Aviso) con el nombre del destinatario, el tamaño total y el nombre del archivo. Bloquear la interfaz de envío esperando respuesta.
  2. Aguardar el `0x07/0x02` (ACK de Consentimiento) del receptor, ruteado por el servidor. Si el segundo byte del payload es `0x00` (rechazo), abortar la operación e informar al usuario. **Timeout:** si no se recibe respuesta en 10 segundos, abortar.
  3. Si el segundo byte es `0x01` (aceptación), leer los primeros 4096 bytes del archivo y enviar `0x04`.
  4. Aguardar el `0x07/0x01` (ACK de Fragmento). No enviar ningún byte adicional hasta recibirlo. **Timeout:** si el ACK no llega en 10 segundos, abortar la transferencia.
  5. Repetir los pasos 3 y 4 hasta que todos los fragmentos hayan sido enviados y confirmados.

---

## Consideraciones Transversales: Seguridad y Tolerancia a Fallos

Esta sección documenta los mecanismos de robustez que deben implementarse durante las Fases 2 y 4. Su ausencia convierte al sistema en un protocolo frágil ante desconexiones inesperadas o procesos que dejan de responder.

### Contexto: TCP vs. Capa de Aplicación

TCP garantiza la entrega de bytes en orden dentro de los buffers del kernel, pero **no puede detectar** que un proceso remoto se congeló sin cerrar el socket. Se definen dos escenarios críticos:

- **Pérdida de paquetes físicos:** Resuelta por el retransmisor TCP del kernel. Completamente transparente para la aplicación.
- **Cliente Fantasma:** Un proceso que deja de vaciar el buffer del socket (por deadlock, *hang* de GUI, etc.) sin enviar `FIN`. El FD permanece abierto pero nadie lee datos; el `client_handler` queda bloqueado indefinidamente en el próximo `write_all` hacia ese cliente.

### Tarea T-FT1: Timeout en el Servidor C — `SO_RCVTIMEO`

- **Módulo:** `network.c` — función `client_handler`.
- **Primitiva del sistema:** `setsockopt(3)` con la opción `SO_RCVTIMEO`. Debe ser la **primera instrucción** del handler, antes del ciclo de lectura.
- **Firma lógica requerida:**

```c
// Primera instrucción de client_handler, antes del ciclo de lectura:
struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```

- **Comportamiento:** Si el socket no recibe ningún byte durante **10 segundos consecutivos**, `read_all` retorna `-1` con `errno == EAGAIN` o `EWOULDBLOCK`. El handler debe tratar este caso como una desconexión: `close(client_fd)` → `remove_user(client_fd)` → retorno del hilo.

### Tarea T-FT2: Timeout en el Cliente Python — ACK Lógico de Transferencia

- **Módulo:** Hilo de envío de archivos (`Tarea 4.3`).
- **Primitiva:** `socket.settimeout(10.0)` aplicada antes de cada `recv()` bloqueante que espera un `0x07` (ACK de Consentimiento `0x07/0x02` o ACK de Fragmento `0x07/0x01`).
- **Firma lógica requerida:**

```python
sock.settimeout(10.0)
try:
    ack_header = recv_all(sock, 5)
    # Interpretar sub-código 0x01 o 0x02
except socket.timeout:
    abort_transfer()
    notify_ui("Transferencia abortada: timeout esperando ACK del destinatario.")
finally:
    sock.settimeout(None)  # Restaurar modo bloqueante
```

### Tarea T-FT3: Flujo de Error en Tránsito — Circuit Breaker

- **Escenario:** Cliente A transmite fragmentos `0x04` hacia Cliente B. El servidor detecta que B se desconectó (violentamente o por expiración del `SO_RCVTIMEO`) y ejecuta `remove_user(fd_b)`.
- **Comportamiento esperado en `router.c`:**
  1. A envía el siguiente `0x04` con destino B.
  2. `route_message` invoca `get_socket_by_name(nombre_b)` → retorna `-1`.
  3. El servidor **frena el enrutamiento** y envía a A el OpCode `0x05` con el texto exacto: `"Transferencia abortada: El destinatario se ha desconectado"`.
  4. El hilo emisor de A recibe el `0x05`, aborta y notifica al usuario.
- **Firma lógica requerida en `router.c`:**

```c
case 0x04: {
    char dest_name[21] = {0};
    memcpy(dest_name, payload, 20);
    int dest_fd = get_socket_by_name(dest_name);
    if (dest_fd == -1) {
        const char *msg = "Transferencia abortada: El destinatario se ha desconectado";
        uint8_t err_sub = 0x01;
        uint32_t err_len = htonl(1 + strlen(msg));
        uint8_t op = 0x05;
        write_all(src_socket, &op, 1);
        write_all(src_socket, &err_len, 4);
        write_all(src_socket, &err_sub, 1);
        write_all(src_socket, msg, strlen(msg));
        return;
    }
    // ... enrutar fragmento normalmente hacia dest_fd
    break;
}
```

---

## Fase 5: Plan de Pruebas y Validación Técnica

### Tarea 5.1: Validación de Deserialización y Enrutamiento

- **Procedimiento:** Conectar Cliente A y B. Enviar mensaje privado de A a B.
- **Resultado Esperado:** B procesa el evento; si A envía a un usuario inexistente, el servidor debe retornar `0x05` de forma inmediata.

### Tarea 5.2: Robustez ante Fallos de Concurrencia

- **Procedimiento:** Abortar proceso del Cliente A (`SIGKILL`) durante una transferencia activa hacia B. Simultáneamente, el Cliente C debe enviar una difusión.
- **Resultado Esperado:** El servidor captura el error de socket de A, libera recursos mediante `remove_user` y procesa la difusión de C sin bloqueos ni fallos de memoria (Segfaults).
