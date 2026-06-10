# Plan de Pruebas de Interfaz (Criterios de Aceptación)

Este documento detalla los pasos a seguir para validar el correcto funcionamiento del chat mediante pruebas manuales en la Interfaz Gráfica de Usuario (GUI).

## Preparación del Entorno
1. Compilar e iniciar el servidor en una terminal (`./servidor_chat`).
2. Abrir tres terminales independientes para instanciar clientes Python (`python3 main.py`).

---

## Escenario 1: Inicio de Sesión y Panel de Usuarios Activos
**Pasos:**
1. En el **Cliente 1**, ingresar el nombre `Bob` y presionar Enter.
2. En el **Cliente 2**, ingresar el nombre `Roxana` y presionar Enter.
3. En el **Cliente 3**, ingresar el nombre `Carlos` y presionar Enter.

**Resultados esperados:**
- Bob y Roxana deben ver un mensaje en gris en la pantalla indicando: `"Carlos se ha conectado"`.
- En la barra lateral (Sidebar) de todos los clientes, deben aparecer listados los tres usuarios (a cada uno le aparecerá la etiqueta `(tú)` en su propio nombre).

---

## Escenario 2: Mensajería de Difusión (Broadcast)
**Pasos:**
1. En la ventana de `Bob`, escribir el mensaje: `¡Hola a todos!` en la barra inferior.
2. Presionar el botón **Enviar** o la tecla *Enter*.

**Resultados esperados:**
- En la pantalla de Bob, el mensaje debe aparecer verde (`→ ¡Hola a todos!`).
- En las pantallas de Roxana y Carlos, el mensaje debe aparecer con formato estándar: `[Bob]: ¡Hola a todos!`.

---

## Escenario 3: Mensajería Privada y Autocompletado
**Pasos:**
1. En la ventana de `Roxana`, hacer clic sobre el nombre de `Bob` en la barra lateral de usuarios conectados.
2. El campo de texto de Roxana debe autocompletarse con `@Bob `.
3. Roxana completa el mensaje: `@Bob Hola Bob, esto es privado`.
4. Presionar **Enviar**.

**Resultados esperados:**
- En la pantalla de Roxana debe aparecer un eco del mensaje: `→ @Bob: Hola Bob, esto es privado`.
- En la pantalla de Bob, el mensaje debe aparecer resaltado (ej. color amarillo) con el texto: `[privado] [Roxana]: Hola Bob, esto es privado`.
- Carlos **no debe ver** este mensaje en su pantalla.

---

## Escenario 4: Pruebas de Fallo - Usuario Inexistente
**Pasos:**
1. En la ventana de `Bob`, escribir manualmente en el campo de texto: `@Fantasmin ¿Estás ahí?`.
2. Presionar **Enviar**.

**Resultados esperados:**
- El servidor no debe crashear.
- En la pantalla de Bob debe aparecer inmediatamente un mensaje de error en color rojo: `[!] Usuario no existe`.

---

## Escenario 5: Envío y Recepción de Archivos (Éxito)
**Pasos:**
1. Crear un archivo de prueba (ej. `imagen.png` o `texto.txt` de 1MB a 5MB).
2. En la ventana de `Carlos`, hacer clic en `Roxana` en el panel lateral (el input queda como `@Roxana `).
3. Hacer clic en el botón de adjunto (`📎`).
4. Se abrirá el explorador de archivos; seleccionar el archivo creado.

**Resultados esperados:**
- En la ventana de Carlos, la interfaz de mensajería se deshabilita temporalmente y aparece la barra de progreso "Enviando: ...".
- La barra se va llenando hasta llegar al 100%.
- En la computadora receptora, debe crearse el archivo en la carpeta de descargas del cliente `Roxana` y debe ser exactamente del mismo tamaño y contenido que el original.

---

## Escenario 6: Desconexión Limpia y Abrupta (Circuit Breaker)
**Pasos:**
1. Cerrar violentamente la ventana de `Carlos` (o presionar `Ctrl+C` en su terminal).
2. **Resultados esperados (Desconexión):** En las pantallas de Bob y Roxana debe aparecer `"Carlos se ha desconectado"` y su nombre debe desaparecer del panel lateral.
3. Bob intenta enviar un archivo grande a Roxana usando el botón `📎`.
4. Mientras la barra de progreso de Bob avanza, **cerrar violentamente** la ventana de Roxana.

**Resultados esperados (Circuit Breaker):**
- El servidor detecta la desconexión de Roxana.
- La barra de transferencia de Bob se detiene y la interfaz se desbloquea.
- Bob recibe un mensaje rojo indicando: `[!] Transferencia abortada: El destinatario se ha desconectado`.

---

## Escenario 7: Límite de Tamaño de Archivo (> 100 MB)
**Pasos:**
1. Intentar enviar un archivo que supere los 100 MB (ej. 105 MB) desde `Bob` a `Carlos` usando el botón `📎`.

**Resultados esperados:**
- La interfaz no debe colapsar.
- El archivo **no** debe comenzar a enviarse (la barra de progreso no debe iniciar).
- Bob debe recibir una alerta o mensaje de error local en rojo: `[!] El archivo supera el límite permitido de 100 MB`.

---

## Escenario 8: Transferencias Concurrentes (Múltiples envíos al mismo usuario)
**Pasos:**
1. `Bob` y `Carlos` preparan un archivo (ej. 10 MB cada uno) para enviar a `Roxana`.
2. Ambos seleccionan a `Roxana` y hacen clic en enviar (`📎`) aproximadamente al mismo tiempo.

**Resultados esperados:**
- Ambos remitentes (`Bob` y `Carlos`) ven sus respectivas barras de progreso avanzar simultáneamente sin bloquearse.
- `Roxana` debe recibir ambos archivos en su carpeta de descargas de forma íntegra.
- Los datos no deben mezclarse; los archivos descargados deben poder abrirse correctamente y sus tamaños deben coincidir exactamente con los originales.

---

## Escenario 9: Mensajería de texto durante Transferencia de Archivo
**Pasos:**
1. `Bob` inicia el envío de un archivo pesado (ej. 50 MB) a `Roxana`.
2. Mientras la barra de progreso está avanzando, `Bob` escribe y envía un mensaje de texto a `Carlos` (ej: `@Carlos ¿todo bien?`).

**Resultados esperados:**
- La interfaz de Bob debe permitir enviar mensajes de texto aunque haya una transferencia en segundo plano.
- `Carlos` recibe el mensaje inmediatamente.
- La transferencia de archivo hacia `Roxana` no se interrumpe y finaliza exitosamente al llegar al 100%.