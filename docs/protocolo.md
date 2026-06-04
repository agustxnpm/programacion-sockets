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
| 2 (0x02) | Mensaje privado | Primero el nombre del destinatario (siempre ocupa 20 lugares, si el nombre es corto se rellena con ceros) + El Texto del mensaje. |
| 6 (0x06) | Mensaje a todos (Difusión) | Solo el texto del mensaje. (No hace falta destinatario porque va para todos). |
| 5 (0x05) | Error | Un número de error (ej. 1) + El texto del error (ej: "Usuario no existe"). |

### Acciones para enviar archivos
Para que la red no colapse, los archivos grandes no se mandan de golpe, se mandan en "fragmentos".

| Número de operación (OpCode) | ¿Qué significa? | ¿Qué viene en el contenido? (Payload) |
|---|---|---|
| 3 (0x03) | Aviso de archivo | Destinatario (20 lugares) + tamaño total del archivo (8 lugares) + nombre del archivo (ej. "foto.png"). |
| 4 (0x04) | Fragmento de archivo | Destinatario (20 lugares) + Los bytes del archivo (Máximo 4096 bytes por envío). |
| 7 (0x07) | Confirmación (ACK) | Un 1 diciendo "Recibí el fragmento, mandame el que sigue". |

## 3. ¿Cómo enviamos un archivo sin romper nada?
Usamos un sistema de "enviar y esperar" (Stop-and-Wait).

1. Cliente A manda la operación 3 (Aviso): "Te voy a mandar un archivo de 10 MB llamado foto.png".
2. El servidor le avisa al Cliente B.
3. Cliente A manda la operación 4 (fragmento): Envía los primeros 4096 bytes de la foto.
4. Cliente A se pausa. No manda nada más por ahora.
5. Cliente B recibe el fragmento, lo guarda y manda la operación 7 (confirmación): "Recibido".
6. El Servidor le pasa esa confirmación al Cliente A.
7. Cliente A al escuchar la confirmación, manda el siguiente fragmento de la foto. Se repite hasta terminar.

## 4. Ejemplos reales (paso a paso)
Para que quede claro cómo viajan los números por el cable de red.

### Ejemplo A: Bob entra al chat (Login)
El usuario "Bob" (que ocupa 3 letras/bytes) abre la app y se conecta. Su cliente de Python manda esta tira de 8 números:

```
[ 1 ] [ 0, 0, 0, 3 ] [ 'B', 'o', 'b' ]
```

- ¿Qué piensa el Servidor en C?

  - Lee el primer número: "Es un 1, esto es un Login".
  - Lee los siguientes 4 números: "El contenido mide 3 bytes".
  - Lee los 3 bytes finales: "Ah, se llama Bob. Lo guardo en mi lista de conectados".

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
