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
| 1 (0x01) | Login | Nombre del usuario |
| 8 (0x08) | Login OK (confirmación) | Sin contenido (0 bytes). El servidor lo envía únicamente al cliente que hizo el Login para confirmar que fue aceptado. Inmediatamente después, el servidor envía un `0x06` al resto de los usuarios notificando el ingreso. |
| 2 (0x02) | Mensaje privado | Primero el nombre del destinatario (siempre ocupa 20 lugares, si el nombre es corto se rellena con ceros) + El Texto del mensaje. |
| 6 (0x06) | Mensaje a todos (Difusión) | Solo el texto del mensaje. (No hace falta destinatario porque va para todos). |
| 5 (0x05) | Error | Un número de error (ej. 1) + El texto del error (ej: "Usuario no existe"). |

### Acciones para enviar archivos
Para que la red no colapse, los archivos grandes no se mandan de golpe, se mandan en "fragmentos".

| Número de operación (OpCode) | ¿Qué significa? | ¿Qué viene en el contenido? (Payload) |
|---|---|---|
| 3 (0x03) | Aviso de archivo | Destinatario (20 lugares) + tamaño total del archivo (8 lugares) + nombre del archivo (ej. "foto.png"). |
| 4 (0x04) | Fragmento de archivo | Destinatario (20 lugares) + Los bytes del archivo (Máximo 4096 bytes por envío). |
| 7 (0x07) | Confirmación (ACK) | Un byte de estado: `1` = aceptar transferencia o confirmar fragmento recibido; `0` = rechazar transferencia. Se usa en dos momentos distintos del protocolo (ver Sección 3). |

## 3. ¿Cómo enviamos un archivo sin romper nada?
Usamos un sistema de "enviar y esperar" (Stop-and-Wait) con un *handshake* de consentimiento previo.

> **Nota Técnica — ¿Por qué controlar el flujo en la capa de aplicación si TCP ya lo hace?**
> TCP garantiza la entrega de proceso a proceso y el orden de los datos en los buffers del kernel del sistema operativo, pero **no conoce la lógica interna de la aplicación**. El control de flujo a nivel de aplicación es obligatorio por dos razones:
> 1. **Evitar el *Head-of-Line Blocking*:** Si enviáramos un archivo de 50 MB de forma continua, ese flujo acapararía el socket bloqueando los mensajes de texto cortos del chat hasta su finalización completa.
> 2. **Sincronización con los tiempos de escritura en disco:** La velocidad de la red supera en órdenes de magnitud los tiempos físicos de escritura en el disco rígido del destinatario. Sin pausas coordinadas, el receptor acumularía datos en memoria que no puede persistir a tiempo.

El protocolo para transferir un archivo tiene los siguientes cinco pasos:

1. **Aviso y consentimiento:** Cliente A envía la operación 3 (`0x03`) al Servidor con el nombre del destinatario, el tamaño total y el nombre del archivo. El Servidor lo rutea al Cliente B.
2. **Respuesta del receptor:** Cliente B decide si acepta la transferencia y responde con la operación 7 (`0x07`): un byte con valor `1` si acepta o `0` si rechaza. El Servidor rutea ese ACK hacia el Cliente A.
3. **Inicio de la transferencia (solo si hubo aceptación):** Si A recibió un `1` en el ACK, envía la operación 4 (`0x04`) con los primeros 4096 bytes del archivo. Si recibió un `0`, cancela la operación.
4. **Pausa obligatoria:** Cliente A se detiene. No envía ningún fragmento adicional hasta recibir confirmación.
5. **ACK de fragmento y continuación:** Cliente B recibe el fragmento, lo escribe en disco y responde con la operación 7 (`0x07`, valor `1`). El Servidor rutea ese ACK hacia A. Al recibirlo, A envía el siguiente fragmento. Los pasos 3 a 5 se repiten hasta completar el archivo.

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

**Paso 2 — Servidor → Bob** (Login OK, `0x08`, payload vacío):
```
[ 8 ] [ 0, 0, 0, 0 ]
```

- ¿Qué piensa el cliente Python de Bob?

  - Lee el `8`: "Recibí un Login OK. Mi sesión fue aceptada".
  - Lee el `0`: "No hay contenido adicional. Puedo habilitar la interfaz del chat".

**Paso 3 — Servidor → Todos los demás** (difusión de ingreso, `0x06`):
Inmediatamente después de enviar el `0x08` a Bob, el servidor envía un mensaje de difusión al resto de los usuarios conectados notificando el nuevo ingreso (por ejemplo: "Bob se ha conectado").

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
