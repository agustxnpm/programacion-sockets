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

---

## Escenario 10: Diálogo de Consentimiento con Emisor Visible y Timeout Automático
**Pasos:**
1. `Bob` selecciona a `Roxana` y envía un archivo por `📎`.
2. En la ventana de `Roxana`, observar el diálogo de consentimiento de transferencia.
3. No presionar aceptar ni rechazar; esperar 30 segundos.

**Resultados esperados:**
- El diálogo de Roxana debe mostrar claramente quién envía el archivo (ej.: `De: Bob`).
- El diálogo debe mostrar cuenta regresiva visible hasta 0.
- Al llegar a 0, el diálogo se cierra automáticamente y se considera rechazo por timeout.
- En Bob, la transferencia se aborta con un mensaje claro de rechazo/timeout, sin congelar la interfaz.

---

## Escenario 11: Selector de Difusión con Botón # General
**Pasos:**
1. Con `Bob`, `Roxana` y `Carlos` conectados, en la barra lateral verificar la presencia del botón `# General`.
2. En `Bob`, hacer clic en `# General`.
3. Enviar el mensaje: `Mensaje de prueba global`.

**Resultados esperados:**
- El botón `# General` debe estar siempre visible y seleccionable.
- El mensaje se envía como difusión a todos los usuarios conectados.
- El comportamiento de difusión no depende de seleccionar el nombre propio en la lista.

---

## Escenario 12: Cancelación Manual de Envío (Botón ✕)
**Pasos:**
1. `Bob` inicia envío de archivo grande (ej. 30 MB o más) hacia `Roxana`.
2. Mientras la barra `Enviando...` avanza, en `Bob` presionar el botón `✕` de la barra de envío.

**Resultados esperados:**
- La transferencia se aborta inmediatamente en Bob.
- La barra de envío desaparece y la interfaz vuelve a estado normal (desbloqueada).
- Roxana no debe quedar bloqueada esperando más datos de esa transferencia.

---

## Escenario 13: Cancelación Manual de Recepción (Botón ✕) y Limpieza de Parcial
**Pasos:**
1. `Bob` inicia envío de archivo grande a `Roxana`.
2. En `Roxana`, cuando aparezca la barra `Recibiendo archivo...`, presionar `✕` antes del 100%.
3. Revisar la carpeta de descargas del cliente receptor.

**Resultados esperados:**
- La recepción se cancela inmediatamente y la barra desaparece.
- El archivo parcial debe eliminarse del disco (no debe quedar corrupto/incompleto guardado).
- Bob recibe notificación de cancelación y su interfaz se recupera sin quedar colgada.

---

## Escenario 14: Desconexión Abrupta del Emisor Durante Transferencia
**Pasos:**
1. `Bob` comienza a enviar un archivo grande a `Roxana`.
2. Mientras la transferencia está en curso, cerrar violentamente el cliente de Bob (`Ctrl+C` o cerrar ventana).

**Resultados esperados:**
- Roxana recibe un mensaje de error/cancelación de transferencia (vía `0x05`) en lugar de quedar esperando indefinidamente.
- La barra de recepción de Roxana se detiene y se limpia.
- Bob desaparece del panel de usuarios activos del resto de clientes con el mensaje de desconexión correspondiente.

---

## Escenario 15: Error de Red Mostrado al Usuario sin Traza Técnica
**Pasos:**
1. Con clientes conectados, detener el servidor abruptamente.
2. Intentar enviar un mensaje o archivo desde cualquier cliente.

**Resultados esperados:**
- La aplicación informa un error de conexión/red en lenguaje entendible para usuario.
- No se muestran trazas crudas del sistema operativo ni stack traces técnicos en la GUI.
- La interfaz se mantiene estable y permite cerrar/reintentar sin colapsar.

---

## Escenario 16: Prueba Dirigida de Seguridad de Nombre de Archivo (Traversal)
**Pasos:**
1. Simular una solicitud de archivo con nombre malicioso (ej.: `../secreto.txt`, `..\\secreto.txt` o ruta absoluta) usando un cliente de prueba/controlado.
2. Entregar esa solicitud al cliente receptor (`Roxana`).

**Resultados esperados:**
- El cliente receptor rechaza el nombre de archivo inválido y no crea archivos fuera de su carpeta de descargas.
- La transferencia se aborta limpiamente.
- La interfaz permanece funcional y muestra una notificación de error comprensible.

---

## Escenario 17: Cierre de Aplicación y Limpieza de Temporales
**Pasos:**
1. Ejecutar uno o más clientes y utilizar la app normalmente.
2. Cerrar la aplicación de forma normal.
3. Revisar el directorio del cliente para verificar residuos temporales.

**Resultados esperados:**
- El cierre no produce errores visibles para el usuario.
- Los archivos temporales de Python (`__pycache__`) se limpian automáticamente al salir.
- En la próxima ejecución, la aplicación inicia normalmente.