# Chat Cliente-Servidor (TP Sockets)

Este proyecto implementa un protocolo de chat a nivel de aplicación sobre TCP usando una arquitectura Cliente-Servidor. El servidor está desarrollado en C y los clientes en Python.

## 1. Requisitos previos e Instalación (Linux / Ubuntu / WSL)

Si estás utilizando una distribución de Linux basada en Debian/Ubuntu (o WSL en Windows), necesitas asegurarte de tener instalados los compiladores de C y el intérprete de Python 3.

Abre tu terminal y ejecuta los siguientes comandos:

```bash
# Actualizar la lista de paquetes
sudo apt update

# Instalar el compilador de C (gcc), herramientas de construcción, Python 3, pip y tkinter
sudo apt install gcc build-essential python3 python3-pip python3-tk -y

# Instalar las dependencias Python del cliente
pip install -r requirements.txt --break-system-packages
 
# Instalar el icono de adjunto
apt install fonts-noto-color-emoji
```

> **Nota WSL/Linux con display:** `customtkinter` requiere un servidor gráfico (X11). En WSL2 moderno (Windows 11) esto funciona automáticamente con WSLg. En WSL1 o Linux sin escritorio necesitás instalar un servidor X (por ejemplo VcXsrv en Windows) y exportar la variable `DISPLAY`.


## 2. Cómo compilar y ejecutar el Servidor (C)

El servidor debe ser lo primero que inicies, ya que los clientes intentarán conectarse a él.

1. Abre una terminal.
2. Navega hasta la carpeta del servidor:
   ```bash
   cd servidor
   ```
3. Compila el código fuente usando `gcc`. Necesitamos enlazar la librería de hilos (`-lpthread`):
   ```bash
   gcc main.c network.c router.c users.c -o servidor_chat -lpthread
   ```
4. Ejecuta el servidor:
   ```bash
   ./servidor_chat
   ```
   *Verás un mensaje indicando: "Servidor escuchando en el puerto 9100". Deja esta terminal abierta.*

## 3. Cómo ejecutar los Clientes (Python)

Para probar la comunicación, simularemos a dos usuarios (por ejemplo, "Alice" y "Bob"). Necesitarás abrir **dos nuevas ventanas o pestañas de terminal**.

En **cada una** de las nuevas terminales, haz lo siguiente:

1. Navega a la carpeta donde se encuentra tu cliente Python:
   ```bash
   cd cliente
   ```
2. Ejecuta el cliente:
   ```bash
   python3 main.py
   ```

## 4. Probando el Protocolo (Qué escribir)

Una vez que tengas las dos terminales de los clientes corriendo, sigue estos pasos para verificar que tu protocolo funciona correctamente:

### Paso A: Login
- **En la Terminal del Cliente 1:** Cuando la consola te pida un nombre de usuario, escribe `Alice` y presiona Enter.
  - *Comportamiento esperado:* El servidor registrará a Alice y el cliente quedará a la espera.
- **En la Terminal del Cliente 2:** Cuando te pida el nombre, escribe `Bob` y presiona Enter.
  - *Comportamiento esperado:* El servidor registrará a Bob. Además, en la pantalla de Alice debería aparecer un mensaje automático del servidor (Difusión / OpCode 0x06) diciendo: *"Bob se ha conectado"*.

### Paso B: Chat Privado y Difusión
- **Mensaje a todos (Difusión):** Desde Bob, simplemente escribe un mensaje (sin poner ningún arroba) y presiona Enter. Esto enviará el mensaje a todos los usuarios conectados en la sala.
- **Mensaje Privado:** Para enviarle un mensaje exclusivamente a Alice, escribe `@Alice` seguido de un espacio y tu mensaje (por ejemplo: `@Alice Hola, ¿cómo estás?`).
- Verifica en la terminal de Alice que el mensaje haya llegado correctamente y que no se hayan mezclado caracteres.

### Paso C: Transferencia de Archivos
Para probar el *Handshake* (Aviso de tamaño) y el *Stop-and-Wait*:
1. Crea un archivo de texto pequeño llamado `prueba.txt` en la misma carpeta que tu cliente de Python.
2. Desde Alice, utiliza el botón en la interfaz destinado a enviar archivos, selecciona tu `prueba.txt` y especifica a `Bob` como destinatario.
3. **Comportamiento esperado según el protocolo:**
   - Alice envía el OpCode `0x03` al servidor con el tamaño.
   - El servidor rutea la petición a Bob.
   - Bob (el Cliente 2) recibe el aviso y acepta la transferencia (`0x07/0x02`).
   - Alice comienza a enviar el contenido en pedazos (`0x04`).
   - Bob confirma cada pedazo (`0x07/0x01`).
   - Revisa la carpeta del Cliente 2 para confirmar que `prueba.txt` se haya guardado correctamente con el mismo tamaño y contenido original.

---
**Nota para apagado:** Para detener el servidor o los clientes, simplemente presiona `Ctrl + C` en sus respectivas terminales.
