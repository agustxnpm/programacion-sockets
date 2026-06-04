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
| 0x07   | ACK Archivo   | [Estado de confirmación (1 byte)]                               |

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

  1. Llama a `read_all` para obtener los 5 bytes de la cabecera.
  2. Desempaqueta el `Payload Length` y convierte el entero de Network Byte Order a Host Byte Order usando `ntohl()`.
  3. Reserva un buffer temporal y llama a `read_all` para obtener los bytes del payload.
  4. Invoca a `route_message(client_fd, opcode, payload, length)`.
  5. Libera el buffer temporal y repite el ciclo.

- Si hay fallo, se procede al cierre del socket y notificación a la Fase 3.

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
- **Salida:** Hilo secundario que procesa cabeceras y payloads, emitiendo callbacks a la interfaz principal para actualizar la vista.

### Tarea 4.3: Controlador de Flujo de Archivos (Chunking)

- **Justificación:** Control de congestión del buffer TCP.
- **Lógica:** Lectura fragmentada del archivo (4096 bytes). Cada envío de `0x04` requiere la recepción obligatoria de un `0x07` (ACK) antes de procesar el siguiente fragmento.

---

## Fase 5: Plan de Pruebas y Validación Técnica

### Tarea 5.1: Validación de Deserialización y Enrutamiento

- **Procedimiento:** Conectar Cliente A y B. Enviar mensaje privado de A a B.
- **Resultado Esperado:** B procesa el evento; si A envía a un usuario inexistente, el servidor debe retornar `0x05` de forma inmediata.

### Tarea 5.2: Robustez ante Fallos de Concurrencia

- **Procedimiento:** Abortar proceso del Cliente A (`SIGKILL`) durante una transferencia activa hacia B. Simultáneamente, el Cliente C debe enviar una difusión.
- **Resultado Esperado:** El servidor captura el error de socket de A, libera recursos mediante `remove_user` y procesa la difusión de C sin bloqueos ni fallos de memoria (Segfaults).
