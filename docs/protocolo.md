# Especificación del Protocolo de Chat
Trabajo Práctico 4 - Redes y Transmisión de Datos

Este documento define cómo se comunican el Cliente (Python) y el Servidor (C). La regla principal es que nunca enviamos texto "suelto" por la red. Todo lo que enviamos va dentro de un formato estricto, como si fuera una carta dentro de un sobre.

## 1. El "Sobre": La cabecera (Header)
Cada vez que un programa le manda algo al otro, SIEMPRE tiene que enviar primero una cabecera de exactamente 5 números (5 bytes). Esta cabecera le dice al que recibe de qué se trata el mensaje y qué tamaño tiene.

La cabecera se divide en dos partes:

1. Número de operación (OpCode) - 1 byte: Es un solo número que dice qué queremos hacer (ejemplo: 1 es Login, 2 es Mensaje).
2. Tamaño del contenido (Payload Length) - 4 bytes: Le dice al que recibe cuántas letras/bytes vienen a continuación. Así el servidor sabe exactamente cuándo termina de leer un mensaje.

## 2. Las operaciones y el contenido (Payload)
Una vez que el servidor o el cliente leen los 5 bytes del encabezado, se fijan cuál es la operación (OpCode) para saber qué viene adentro. Aquí están todas las acciones permitidas en nuestro chat:

> **Límite de Usuarios:** El servidor admite un máximo de **100 usuarios conectados simultáneamente**. Si un cliente intenta iniciar sesión (Login) cuando el servidor está lleno, o si usa un nombre que ya se encuentra en uso, el servidor rechazará la conexión enviando un OpCode de Error (`0x05`) y cerrará el socket.

| Número de operación (OpCode) | ¿Qué significa? | ¿Qué viene en el contenido? (Payload) |
|---|---|---|
| 1 (0x01) | Login | Nombre del usuario. |
| 2 (0x02) | Mensaje privado | Primero el nombre del destinatario (siempre ocupa 20 lugares, si el nombre es corto se rellena con ceros) + El Texto del mensaje. **Convención de identificación del emisor:** dado que el servidor reenvía el payload íntegro sin agregar el nombre del remitente, los clientes **deben** incluir su propio nombre al inicio del texto con el formato `[nombre_emisor]: mensaje`. El receptor parsea ese prefijo para mostrar quién envió el mensaje. |
| 5 (0x05) | Error | Un número de error (ej. 1) + El texto del error (ej: "Usuario no existe"). |
| 6 (0x06) | Mensaje a todos (Difusión) | Solo el texto del mensaje. El servidor también usa este opcode para **eventos de sistema**: al conectarse un usuario emite `"X se ha conectado"` y al desconectarse emite `"X se ha desconectado"`. **Convención de identificación del emisor (mensajes de usuario):** cuando un cliente envía una difusión, aplica la misma convención que en 0x02: antepone `[nombre_emisor]: ` al texto. Los mensajes de sistema emitidos por el servidor **no** usan este prefijo; el receptor los distingue por los sufijos `" se ha conectado"` / `" se ha desconectado"`. |
| 7 (0x07) | **ACK Polimórfico** (Confirmación Generalizada) | El primer byte del payload es el **Sub-Código de Tipo**, que determina el significado del mensaje. Ver tabla de Sub-Códigos a continuación. |
| 255 (0xFF) | Heartbeat (Keepalive) | Ninguno (payload vacío). Enviado periódicamente por el cliente para evitar que el `SO_RCVTIMEO` del servidor lo detecte como cliente fantasma. **El servidor ignora silenciosamente este opcode.** |

### Sub-Códigos del OpCode `0x07` (ACK Polimórfico)

El primer byte del payload del `0x07` es el **Sub-Código de Tipo**. El servidor lo enruta como cualquier otro mensaje; el receptor interpreta el sub-código para determinar la acción a tomar.

| Sub-Código | Nombre | Payload Completo | Descripción |
|---|---|---|---|
| `0x01` | ACK de Fragmento de Archivo | `[0x01][Emisor(20)]` (21 bytes) | Confirma la recepción de un chunk e identifica explícitamente al emisor para que el servidor enrute ACKs concurrentes sin ambigüedad. |
| `0x02` | ACK de Consentimiento de Archivo | `[0x02][0x01/0x00][Emisor(20)]` (22 bytes) | Respuesta del destinatario al Aviso de Archivo (`0x03`), incluyendo el emisor al que aplica la decisión. |
| `0x03` | ACK de Login Exitoso | `[0x03]` (1 byte) | Confirmación del servidor al cliente de que su Login fue aceptado. **Reemplaza el antiguo `0x08`**. |

### Acciones para enviar archivos
Para que la red no colapse, los archivos grandes no se mandan de golpe, se mandan en "fragmentos".

| Número de operación (OpCode) | ¿Qué significa? | ¿Qué viene en el contenido? (Payload) |
|---|---|---|
| 3 (0x03) | Aviso de archivo | **Emisor -> Servidor:** Destinatario (20) + tamaño (8) + nombre. **Servidor -> Receptor:** Emisor (20) + tamaño (8) + nombre. |
| 4 (0x04) | Fragmento de archivo | **Emisor -> Servidor:** Destinatario (20) + bytes. **Servidor -> Receptor:** Emisor (20) + bytes (máx. 65536). |

## 3. ¿Cómo enviamos un archivo sin romper nada?
Usamos un sistema de "enviar y esperar" (Stop-and-Wait) con un *handshake* de consentimiento previo.

**¿Por qué avisamos el tamaño total del archivo previamente?**
Enviar el tamaño total en el aviso inicial (`0x03`) cumple dos roles vitales:
1. **Prevención del receptor:** Permite al destinatario verificar si tiene suficiente espacio en disco o si simplemente desea rechazar un archivo excesivamente grande antes de empezar a recibir miles de fragmentos.
2. **Protección del servidor:** Le permite al servidor establecer y hacer cumplir un **límite de tamaño máximo** (por ejemplo, 100 MB), protegiendo la red de transferencias abusivas que puedan saturar el ancho de banda.

> **Nota Técnica — ¿Por qué controlar el flujo en la capa de aplicación si TCP ya lo hace?**
> TCP garantiza la entrega de proceso a proceso y el orden de los datos en los buffers del kernel del sistema operativo, pero **no conoce la lógica interna de la aplicación**. El control de flujo a nivel de aplicación es obligatorio por dos razones:
> 1. **Evitar el *Head-of-Line Blocking*:** Si enviáramos un archivo de 50 MB de forma continua, ese flujo acapararía el socket bloqueando los mensajes de texto cortos del chat hasta su finalización completa.
> 2. **Sincronización con los tiempos de escritura en disco:** La velocidad de la red supera en órdenes de magnitud los tiempos físicos de escritura en el disco rígido del destinatario. Sin pausas coordinadas, el receptor acumularía datos en memoria que no puede persistir a tiempo.

El protocolo para transferir un archivo tiene los siguientes cinco pasos:

1. **Aviso y validación de tamaño:** Cliente A envía la operación `0x03` al Servidor con el nombre del destinatario, el tamaño total y el nombre del archivo. El Servidor primero lee este tamaño; **si supera el valor máximo permitido (ej. 100 MB), rechaza la petición** devolviendo inmediatamente un error `0x05` a A. Si el tamaño es válido, el Servidor lo rutea al Cliente B. *(Nota: El cliente también debe implementar ciertos mecanismos relacionados a este límite de capacidad antes de enviar, pero sus detalles se omiten en esta especificación).*
2. **Respuesta del receptor:** Cliente B decide si acepta la transferencia y responde con `0x07/0x02`: payload `[0x02, 0x01/0x00, Emisor(20)]`.
3. **Inicio de la transferencia (solo si hubo aceptación):** Si A recibió el ACK de Consentimiento con segundo byte `0x01`, envía la operación `0x04` con los primeros 65536 bytes del archivo. Si el segundo byte es `0x00`, cancela la operación.
4. **Pausa obligatoria:** Cliente A se detiene. No envía ningún fragmento adicional hasta recibir confirmación.
5. **ACK de fragmento y continuación:** Cliente B recibe el fragmento, lo escribe en disco y responde con `0x07/0x01`: payload `[0x01, Emisor(20)]`. El servidor rutea ese ACK al emisor correcto. Los pasos 3 a 5 se repiten hasta completar el archivo.

> **Protección contra fragmentos maliciosos:** Para evitar saturación de memoria o abuso de red, tanto el servidor (antes de enrutar) como el cliente receptor (antes de escribir a disco) validan estrictamente que los fragmentos (`0x04`) no superen el límite máximo de **65536 bytes (64 KB)**. Si se detecta un fragmento de mayor tamaño, la transferencia se aborta inmediatamente y se emite un error `0x05`.

### 3.1 Nota técnica para Servidores: Rastreo de Emisores
El servidor usa un esquema de **enrutado dirigido por emisor**:

1. Al reenviar `0x03/0x04` al receptor, reescribe el campo inicial de 20 bytes con el nombre del emisor.
2. El receptor responde ACKs (`0x07/0x02` y `0x07/0x01`) incluyendo ese emisor en el payload.
3. El servidor extrae dicho emisor y reenvía el ACK al socket correcto.

Con este diseño, el sistema **sí soporta transferencias simultáneas al mismo destinatario** (múltiples emisores -> un receptor).

## 4. Ejemplos reales (paso a paso)
Para que quede claro cómo viajan los números por el cable de red.

### Ejemplo A: Bob entra al chat (Login con confirmación)
El usuario "Bob" (que ocupa 3 letras/bytes) abre la app y se conecta. El intercambio ahora tiene dos pasos de ida y vuelta.

**Paso 1 — Bob → Servidor** (solicitud de Login):
```
[ 1 ] [ 0, 0, 0, 3 ] [ 'B', 'o', 'b' ]
```

- ¿Qué piensa el Servidor en C?

  - Lee el `1`: "Es un Login".
  - Lee el `3`: "El contenido mide 3 bytes".
  - Lee los 3 bytes: "Se llama Bob. Nombre disponible, lo agrego a la lista de conectados".

**Paso 2 — Servidor → Bob** (Login OK, `0x07/0x03`, 1 byte de payload):
```
[ 7 ] [ 0, 0, 0, 1 ] [ 0x03 ]
```

- ¿Qué piensa el cliente Python de Bob?

  - Lee el `7`: "Es un ACK Polimórfico (`0x07`)".
  - Lee el `1`: "Hay 1 byte de contenido".
  - Lee el byte `0x03`: "Sub-Código 3 → Login Exitoso. Mi sesión fue aceptada. Puedo habilitar la interfaz del chat".

**Paso 3 — Servidor → Todos los demás** (difusión de ingreso, `0x06`):
Inmediatamente después de enviar el `0x07/0x03` a Bob, el servidor envía un mensaje de difusión al resto de los usuarios conectados notificando el nuevo ingreso (por ejemplo: "Bob se ha conectado").

### Ejemplo B: Bob le manda un mensaje a Alice
Bob manda el texto "Hola" (4 letras) a "Alice". Como el nombre del destinatario siempre ocupa 20 espacios fijos (para que sea fácil de separar), el contenido total mide 24 bytes (20 del nombre + 4 del "Hola").

```
[ 2 ] [ 0, 0, 0, 24 ] [ 'A', 'l', 'i', 'c', 'e', (15 ceros vacíos)... ] [ 'H', 'o', 'l', 'a' ]
```

- ¿Qué piensa el Servidor en C?

  - Lee el 2: "Es un mensaje privado".
  - Lee el 24: "Tengo que leer 24 bytes".
  - Agarra los primeros 20 bytes del contenido: "Busco en mi lista si Alice está conectada".
  - Encuentra a Alice, agarra el mensaje completo y se lo reenvía a ella.

### Ejemplo C: Bob se desconecta
Bob cierra la aplicación (o pierde la conexión). El servidor detecta la desconexión al retornar `read_all()` `0` o `-1` en el hilo de Bob.

**Servidor → Todos los demás** (difusión de salida, `0x06`):
```
[ 6 ] [ 0, 0, 0, 21 ] [ 'B', 'o', 'b', ' ', 's', 'e', ' ', 'h', 'a', ' ', 'd', 'e', 's', 'c', 'o', 'n', 'e', 'c', 't', 'a', 'd', 'o' ]
```
- El servidor obtiene el nombre de Bob a partir de su FD, lo elimina de la lista activa, cierra el socket, y luego envía el `0x06` a todos los usuarios restantes.
- Cada cliente que recibe este mensaje reconoce el sufijo `" se ha desconectado"` y elimina a Bob de su panel de usuarios activos.
## 5. Tolerancia a Fallos y Gestión de Timeouts

### 5.1 Separación de Responsabilidades: TCP vs. Capa de Aplicación

TCP garantiza la entrega de bytes en orden y sin duplicados dentro de los buffers del kernel del sistema operativo. La **pérdida física de paquetes** es completamente transparente para la aplicación: el kernel los retransmite automáticamente.

Sin embargo, TCP no resuelve el escenario de **pérdida de ACKs lógicos de aplicación**. Un ACK lógico es la confirmación que nuestra aplicación envía (por ejemplo, `0x07/0x01`) para indicar que procesó correctamente la información a nivel semántico (escribió el chunk en disco, habilitó la UI, etc.). Si el proceso receptor se congela **sin cerrar el socket**, TCP mantiene la conexión abierta pero nadie lee los datos: el emisor queda bloqueado indefinidamente.

### 5.2 El Problema del Cliente Fantasma

Un **Cliente Fantasma** ocurre cuando un proceso deja de vaciar el buffer del socket (por un *hang* de la GUI, un deadlock, una señal no manejada, etc.) sin enviar un segmento `FIN` de TCP. Desde la perspectiva del servidor:

- El FD del cliente sigue abierto y válido.
- Cualquier `write_all` hacia ese FD puede bloquear al `client_handler` indefinidamente (el send-buffer del kernel se llena y la llamada no retorna).
- El `client_handler` queda zombi, consumiendo un hilo del pool y un FD del sistema.

**Solución:** Configurar un timeout de recepción con `SO_RCVTIMEO` para que el servidor detecte la inactividad y fuerce el cierre del socket.

### 5.3 Timeout en el Servidor C: `SO_RCVTIMEO` (10 segundos)

Cada `client_handler` debe configurar el timeout de recepción al inicio de su ejecución, antes de entrar al ciclo de lectura:

```c
struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```

Si el socket no recibe ningún dato durante **10 segundos consecutivos**, la próxima llamada a `recv()` retorna `-1` con `errno == EAGAIN` o `EWOULDBLOCK`. Al detectar este error, el servidor debe:

1. Forzar el cierre del socket: `close(client_fd)`.
2. Eliminar al usuario de la lista activa: `remove_user(client_fd)`.
3. Finalizar el hilo.

### 5.4 Timeout en el Cliente Python: Espera de ACK Lógico (30 segundos)

El hilo de envío de archivos del Cliente Python aplica timeout al bloquearse esperando el `0x07` de confirmación. Si el emisor pasa más de **30 segundos** sin recibir ACK lógico, aborta la transferencia:

```python
CONSENT_TIMEOUT_SEC = 30
CHUNK_ACK_TIMEOUT_SEC = 30

if not consent_event.wait(timeout=CONSENT_TIMEOUT_SEC):
  abort_transfer()

if not chunk_ack_event.wait(timeout=CHUNK_ACK_TIMEOUT_SEC):
  abort_transfer()
```

### 5.5 Flujo de Error en Tránsito: Cierre de Circuito (Circuit Breaker)

**Escenario:** El Cliente A está transmitiendo fragmentos (`0x04`) hacia el Cliente B. El servidor detecta que B se desconectó (por desconexión violenta o por expiración del `SO_RCVTIMEO`) y ejecuta `remove_user(fd_b)`.

**Comportamiento del protocolo:**

1. A envía el siguiente fragmento `0x04` indicando a B como destinatario.
2. El servidor, en `route_message`, invoca `get_socket_by_name(nombre_b)` y obtiene `-1`.
3. En lugar de descartar el fragmento silenciosamente, el servidor **frena el enrutamiento** y responde a A con el OpCode `0x05` (Error) y el texto exacto:

   > *"Transferencia abortada: el destinatario no está conectado."*

4. El hilo de envío de A recibe el `0x05`, aborta inmediatamente la transferencia y notifica al usuario.

Este mecanismo implementa el patrón **Circuit Breaker**: el servidor actúa como intermediario que detecta la ruptura del circuito y notifica proactivamente al emisor, evitando que quede bloqueado hasta que expire su propio timeout.

### 5.6 Heartbeat del Cliente: OpCode `0xFF`

El **Heartbeat** es un mecanismo complementario al `SO_RCVTIMEO` del servidor. Sin él, un cliente legítimamente inactivo (sin mensajes que enviar durante más de 10 segundos) sería expulsado por el servidor como si fuera un cliente fantasma.

**Funcionamiento:**
- El cliente envía un paquete con **OpCode `0xFF`** y **payload vacío (0 bytes)** cada **8 segundos** mientras está conectado:
```
[ 255 ] [ 0, 0, 0, 0 ]
```
- El servidor **no tiene un `case 0xFF`** en su `switch-case` y descarta el paquete silenciosamente.
- Pero el hecho de recibir cualquier dato resetea el temporizador interno del `SO_RCVTIMEO`, por lo que el servidor nunca expira la conexión de un cliente que envía heartbeats.

**¿Por qué 8 segundos y no 10?** El intervalo de 8 s es deliberadamente menor al timeout del servidor (10 s) para garantizar que al menos un heartbeat llegue antes del vencimiento, incluso ante variaciones de latencia.

### 5.7 Tabla de Transferencias Activas y Cancelación Inversa

El escenario anterior cubre el caso en que el **receptor** se desconecta después de que el emisor ya empezó a enviar chunks. Sin embargo, el caso inverso —el **emisor** se desconecta a mitad de transferencia— no quedaba cubierto por el Circuit Breaker, ya que el servidor cerraba el socket del emisor sin notificar al receptor, dejándolo bloqueado indefinidamente esperando chunks que nunca llegarían.

**Solución: tabla de transferencias activas en el servidor.**

El servidor (`router.c`) mantiene dos arreglos estáticos protegidos por mutex:

- `transfer_to_recv[sender_fd]` → fd del receptor asociado a ese emisor.
- `transfer_to_send[receiver_fd]` → fd del emisor asociado a ese receptor.

**Ciclo de vida de una entrada:**

| Evento | Operación en la tabla |
|---|---|
| Servidor reenvía `0x03` al receptor | `register_transfer(sender_fd, receiver_fd)` |
| Receptor responde `0x07/0x02/0x00` (rechazo) | `clear_transfer(sender_fd, receiver_fd)` |
| Archivo completo (emisor recibe último ACK) | `clear_transfer(sender_fd, receiver_fd)` |
| Cualquiera de los dos se desconecta | `cancel_active_transfer(fd)` — limpia tabla y notifica al peer |

**`cancel_active_transfer(fd)`** debe llamarse desde `network.c` **antes** de `handle_disconnect(fd)`, para que el socket del peer todavía esté abierto al momento de escribir el `0x05`:

```c
// network.c — al salir del ciclo client_handler:
cancel_active_transfer(client_fd);   // notifica al peer si hay transferencia activa
handle_disconnect(client_fd);        // difunde desconexión, cierra socket y elimina de la lista
```

**Mensaje enviado al peer:**

- Si quien se desconecta es el **emisor**:
  > *"Transferencia cancelada: el emisor se ha desconectado."*

- Si quien se desconecta es el **receptor**:
  > *"Transferencia cancelada: el receptor se ha desconectado."*

**Comportamiento del cliente receptor al recibir `0x05` durante una transferencia:**
El texto del error contiene `"Transferencia cancelada"`. Al detectarlo, el cliente cancela la recepción activa (cierra y elimina el archivo parcial), oculta la barra de progreso y muestra el mensaje de error en el chat.

### 5.8 Timeout de Consentimiento en el Receptor

Cuando el receptor recibe un aviso de archivo (`0x03`) se le muestra un diálogo modal. Si el usuario no responde en **30 segundos**, el diálogo se cierra automáticamente y el cliente envía `0x07/0x02/0x00` (rechazo) al servidor, liberando el estado de espera tanto en el receptor como en el emisor (que recibirá el ACK de rechazo).
