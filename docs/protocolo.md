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

| Número de operación (OpCode) | ¿Qué significa? | ¿Qué viene en el contenido? (Payload) |
|---|---|---|
| 1 (0x01) | Login | Nombre del usuario. |
| 2 (0x02) | Mensaje privado | Primero el nombre del destinatario (siempre ocupa 20 lugares, si el nombre es corto se rellena con ceros) + El Texto del mensaje. |
| 5 (0x05) | Error | Un número de error (ej. 1) + El texto del error (ej: "Usuario no existe"). |
| 6 (0x06) | Mensaje a todos (Difusión) | Solo el texto del mensaje. (No hace falta destinatario porque va para todos). |
| 7 (0x07) | **ACK Polimórfico** (Confirmación Generalizada) | El primer byte del payload es el **Sub-Código de Tipo**, que determina el significado del mensaje. Ver tabla de Sub-Códigos a continuación. |

### Sub-Códigos del OpCode `0x07` (ACK Polimórfico)

El primer byte del payload del `0x07` es el **Sub-Código de Tipo**. El servidor lo enruta como cualquier otro mensaje; el receptor interpreta el sub-código para determinar la acción a tomar.

| Sub-Código | Nombre | Payload Completo | Descripción |
|---|---|---|---|
| `0x01` | ACK de Fragmento de Archivo | `[0x01]` (1 byte) | Confirma la recepción de un chunk. Lo envía el receptor luego de escribir el fragmento en disco. |
| `0x02` | ACK de Consentimiento de Archivo | `[0x02, 0x01]` (aceptar) ó `[0x02, 0x00]` (rechazar) (2 bytes) | Respuesta del destinatario al Aviso de Archivo (`0x03`). |
| `0x03` | ACK de Login Exitoso | `[0x03]` (1 byte) | Confirmación del servidor al cliente de que su Login fue aceptado. **Reemplaza el antiguo `0x08`**. |

### Acciones para enviar archivos
Para que la red no colapse, los archivos grandes no se mandan de golpe, se mandan en "fragmentos".

| Número de operación (OpCode) | ¿Qué significa? | ¿Qué viene en el contenido? (Payload) |
|---|---|---|
| 3 (0x03) | Aviso de archivo | Destinatario (20 lugares) + tamaño total del archivo (8 lugares) + nombre del archivo (ej. "foto.png"). |
| 4 (0x04) | Fragmento de archivo | Destinatario (20 lugares) + Los bytes del archivo (Máximo 4096 bytes por envío). |

## 3. ¿Cómo enviamos un archivo sin romper nada?
Usamos un sistema de "enviar y esperar" (Stop-and-Wait) con un *handshake* de consentimiento previo.

> **Nota Técnica — ¿Por qué controlar el flujo en la capa de aplicación si TCP ya lo hace?**
> TCP garantiza la entrega de proceso a proceso y el orden de los datos en los buffers del kernel del sistema operativo, pero **no conoce la lógica interna de la aplicación**. El control de flujo a nivel de aplicación es obligatorio por dos razones:
> 1. **Evitar el *Head-of-Line Blocking*:** Si enviáramos un archivo de 50 MB de forma continua, ese flujo acapararía el socket bloqueando los mensajes de texto cortos del chat hasta su finalización completa.
> 2. **Sincronización con los tiempos de escritura en disco:** La velocidad de la red supera en órdenes de magnitud los tiempos físicos de escritura en el disco rígido del destinatario. Sin pausas coordinadas, el receptor acumularía datos en memoria que no puede persistir a tiempo.

El protocolo para transferir un archivo tiene los siguientes cinco pasos:

1. **Aviso y consentimiento:** Cliente A envía la operación `0x03` al Servidor con el nombre del destinatario, el tamaño total y el nombre del archivo. El Servidor lo rutea al Cliente B.
2. **Respuesta del receptor:** Cliente B decide si acepta la transferencia y responde con la operación `0x07`, sub-código `0x02` (ACK de Consentimiento): payload `[0x02, 0x01]` si acepta o `[0x02, 0x00]` si rechaza. El Servidor rutea ese ACK hacia el Cliente A.
3. **Inicio de la transferencia (solo si hubo aceptación):** Si A recibió el ACK de Consentimiento con segundo byte `0x01`, envía la operación `0x04` con los primeros 4096 bytes del archivo. Si el segundo byte es `0x00`, cancela la operación.
4. **Pausa obligatoria:** Cliente A se detiene. No envía ningún fragmento adicional hasta recibir confirmación.
5. **ACK de fragmento y continuación:** Cliente B recibe el fragmento, lo escribe en disco y responde con la operación `0x07`, sub-código `0x01` (ACK de Fragmento): payload `[0x01]`. El Servidor rutea ese ACK hacia A. Al recibirlo, A envía el siguiente fragmento. Los pasos 3 a 5 se repiten hasta completar el archivo.

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

### 5.4 Timeout en el Cliente Python: Espera de ACK Lógico (10 segundos)

El hilo de envío de archivos del Cliente Python debe aplicar un timeout al bloquearse esperando el `0x07` de confirmación. Si el emisor pasa más de **10 segundos** sin recibir el ACK de la aplicación destino, debe abortar la transferencia:

```python
sock.settimeout(10.0)
try:
    ack_header = recv_all(sock, 5)
    # Interpretar sub-código del ACK (0x01 o 0x02)
except socket.timeout:
    abort_transfer()
    notify_ui("Transferencia abortada: timeout esperando confirmación del destinatario.")
finally:
    sock.settimeout(None)  # Restaurar modo bloqueante
```

### 5.5 Flujo de Error en Tránsito: Cierre de Circuito (Circuit Breaker)

**Escenario:** El Cliente A está transmitiendo fragmentos (`0x04`) hacia el Cliente B. El servidor detecta que B se desconectó (por desconexión violenta o por expiración del `SO_RCVTIMEO`) y ejecuta `remove_user(fd_b)`.

**Comportamiento del protocolo:**

1. A envía el siguiente fragmento `0x04` indicando a B como destinatario.
2. El servidor, en `route_message`, invoca `get_socket_by_name(nombre_b)` y obtiene `-1`.
3. En lugar de descartar el fragmento silenciosamente, el servidor **frena el enrutamiento** y responde a A con el OpCode `0x05` (Error) y el texto exacto:

   > *"Transferencia abortada: El destinatario se ha desconectado"*

4. El hilo de envío de A recibe el `0x05`, aborta inmediatamente la transferencia y notifica al usuario.

Este mecanismo implementa el patrón **Circuit Breaker**: el servidor actúa como intermediario que detecta la ruptura del circuito y notifica proactivamente al emisor, evitando que quede bloqueado hasta que expire su propio timeout.
