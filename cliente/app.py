"""
ChatApp — orquestador central de la aplicación.

Responsabilidades:
  - Instanciar NetworkClient, LoginView y ChatView.
  - Gestionar la navegación entre vistas (Login ↔ Chat).
  - Ser el único router de paquetes entrantes (_on_packet).
  - Delegar lógica de transferencia a FileTransferController / FileReceiver.
  - Rastrear usuarios conectados a partir de broadcasts del servidor.

Convención de mensajes (client-side):
  - Los mensajes salientes llevan siempre el prefijo "[emisor]: " en el texto
    del payload, de modo que el receptor pueda identificar al remitente.
    (El protocolo 0x02/0x04 no incluye el nombre del emisor en la cabecera.)

Regla de hilo: _on_packet se registra como lambda ... root.after(0, ...)
en el constructor, por lo que SIEMPRE se ejecuta en el hilo GUI. Ningún widget
se toca desde el hilo listener ni desde el hilo de transferencia.
"""
import customtkinter as ctk
from pathlib import Path

from network import NetworkClient
import protocol
from file_transfer import FileTransferController, FileReceiver
from views.login_view import LoginView
from views.chat_view import ChatView
from views.consent_dialog import FileConsentDialog

DEFAULT_HOST = "127.0.0.1"
PORT         = 9100
DOWNLOAD_DIR = Path.home() / "Descargas"

# Patrones que el servidor emite como difusión (router.c, caso 0x01 y handle_disconnect)
_CONNECT_SUFFIX    = " se ha conectado"
_DISCONNECT_SUFFIX = " se ha desconectado"


class ChatApp:
    def __init__(self, root: ctk.CTk):
        self._root = root
        self._root.title("Chat")
        self._root.geometry("760x540")
        self._root.resizable(True, True)
        self._root.minsize(620, 440)

        self._username  = ""
        self._host = DEFAULT_HOST
        self._file_ctrl: FileTransferController | None = None
        self._file_recvs: dict[str, FileReceiver] = {}

        # on_packet siempre se despacha al hilo GUI vía root.after
        self._net = NetworkClient(
            on_packet=lambda op, data: self._root.after(0, self._on_packet, op, data)
        )

        container = ctk.CTkFrame(self._root, fg_color="transparent")
        container.pack(fill="both", expand=True)

        self._login_view = LoginView(container, on_connect=self._do_login)
        self._chat_view  = ChatView(
            container,
            on_send_private   = self._send_private,
            on_send_broadcast = self._send_broadcast,
            on_send_file      = self._start_file_send,
        )

        self._show_login()

    # ── Navegación ────────────────────────────────────────────────────────

    def _show_login(self):
        self._chat_view.pack_forget()
        self._login_view.pack(fill="both", expand=True)

    def _show_chat(self):
        self._login_view.pack_forget()
        self._chat_view.pack(fill="both", expand=True)
        self._chat_view.set_header(self._username)

    # ── Acciones del usuario ──────────────────────────────────────────────

    def _do_login(self, name: str, host: str):
        self._username = name
        self._host = host
        try:
            self._net.connect(self._host, PORT)
            self._net.send(protocol.build_login(name))
        except OSError as e:
            self._login_view.set_connecting(False)
            self._login_view.set_status(f"Error de conexión: {e}", error=True)

    def _send_private(self, dest: str, text: str):
        """Antepone el nombre del emisor para que el receptor lo muestre."""
        full = f"[{self._username}]: {text}"
        self._net.send(protocol.build_private(dest, full))
        self._chat_view.show_private_sent(dest, text)

    def _send_broadcast(self, text: str):
        """Antepone el nombre del emisor para que los demás lo muestren."""
        full = f"[{self._username}]: {text}"
        self._net.send(protocol.build_broadcast(full))
        self._chat_view.show_broadcast_sent(full)

    def _start_file_send(self, dest: str, filepath: str):
        path = Path(filepath)
        self._chat_view.set_transfer_active(True, path.name)
        self._file_ctrl = FileTransferController(
            net       = self._net,
            dest      = dest,
            filepath  = path,
            root      = self._root,
            callbacks = {
                'on_done':     self._on_transfer_done,
                'on_error':    self._on_transfer_error,
                'on_progress': self._chat_view.update_transfer_progress,
            },
        )
        self._file_ctrl.start()

    # ── Router central de paquetes ────────────────────────────────────────
    # Siempre ejecutado en el hilo GUI (vía root.after en el constructor).

    def _on_packet(self, opcode: int, payload: bytes):
        # opcode -1 = señal sintética de desconexión del NetworkClient
        if opcode == -1:
            self._on_disconnected()
            return

        if opcode == 0x07:
            self._handle_ack(payload)

        elif opcode == 0x02:
            self._handle_incoming_private(payload)

        elif opcode == 0x06:
            self._handle_broadcast(payload)

        elif opcode == 0x05:
            msg = payload[1:].decode('utf-8', errors='replace') if payload else "Error desconocido"
            if self._file_ctrl:
                self._file_ctrl.abort()
                self._chat_view.set_transfer_active(False)
                self._file_ctrl = None
            self._chat_view.show_error(msg)

        elif opcode == 0x03:
            self._handle_incoming_file_notice(payload)

        elif opcode == 0x04:
            self._handle_incoming_chunk(payload)

        # opcode 0xFF (heartbeat echo) y cualquier otro se ignoran silenciosamente

    def _handle_ack(self, payload: bytes):
        if not payload:
            return
        subcode = payload[0]

        if subcode == 0x03:
            # Login aceptado por el servidor
            self._on_login_ok()

        elif subcode == 0x01 and self._file_ctrl:
            self._file_ctrl.notify_chunk_ack()

        elif subcode == 0x02 and self._file_ctrl:
            accepted = len(payload) > 1 and payload[1] == 0x01
            self._file_ctrl.notify_consent(accepted)

    def _handle_incoming_private(self, payload: bytes):
        """
        Mensaje privado recibido.
        Intenta extraer el nombre del emisor del texto con formato '[nombre]: msg'
        para agregarlo al sidebar si aún no está.
        """
        text = protocol.parse_private(payload)
        if text.startswith('[') and ']: ' in text:
            sender = text[1:text.index(']: ')]
            self._chat_view.add_user(sender)
        self._chat_view.show_private(text)

    def _handle_broadcast(self, payload: bytes):
        text = payload.decode('utf-8', errors='replace')

        # El servidor emite "X se ha conectado" al hacer login (router.c, 0x01)
        if text.endswith(_CONNECT_SUFFIX) and not text.startswith('['):
            username = text[: -len(_CONNECT_SUFFIX)]
            self._chat_view.add_user(username)
            self._chat_view.show_system(text)
            return

        # El servidor emite "X se ha desconectado" en handle_disconnect
        if text.endswith(_DISCONNECT_SUFFIX) and not text.startswith('['):
            username = text[: -len(_DISCONNECT_SUFFIX)]
            self._chat_view.remove_user(username)
            self._chat_view.show_system(text)
            # Abortar transferencia activa si el destinatario era ese usuario
            if self._file_ctrl and self._file_ctrl._dest == username:
                self._file_ctrl.abort()
                self._chat_view.set_transfer_active(False)
                self._chat_view.show_error(
                    f"Transferencia: '{username}' se ha desconectado."
                )
                self._file_ctrl = None
            return

        self._chat_view.show_broadcast(text)

    def _handle_incoming_file_notice(self, payload: bytes):
        if len(payload) < protocol.MAX_USERNAME_LEN + 8:
            return
        sender = protocol.parse_transfer_peer(payload)
        size, filename = protocol.parse_file_notice(payload)

        def on_accept():
            if sender in self._file_recvs:
                self._net.send(protocol.build_transfer_ack_consent(sender, False))
                self._chat_view.show_error(
                    f"Transferencia: Ya hay una recepción activa desde '{sender}'."
                )
                return

            self._net.send(protocol.build_transfer_ack_consent(sender, True))
            self._file_recvs[sender] = FileReceiver(filename, size, DOWNLOAD_DIR)
            if size >= 1024 * 1024:
                size_str = f"{size / (1024 * 1024):.2f} MB"
            else:
                size_str = f"{size / 1024:.1f} KB"
            self._chat_view.show_system(
                f"Recibiendo '{filename}' desde '{sender}' ({size_str})..."
            )

        def on_reject():
            self._net.send(protocol.build_transfer_ack_consent(sender, False))
            self._chat_view.show_system(f"Archivo '{filename}' de '{sender}' rechazado.")

        FileConsentDialog(self._root, filename, size, on_accept, on_reject)

    def _handle_incoming_chunk(self, payload: bytes):
        if len(payload) < protocol.MAX_USERNAME_LEN:
            return
        sender = protocol.parse_transfer_peer(payload)
        file_recv = self._file_recvs.get(sender)
        if file_recv is None:
            self._chat_view.show_error(
                f"Transferencia: fragmento recibido sin sesión activa desde '{sender}'."
            )
            return

        chunk_data = payload[protocol.MAX_USERNAME_LEN:]
        try:
            file_recv.write_chunk(chunk_data)
        except OSError as e:
            file_recv.close()
            del self._file_recvs[sender]
            self._chat_view.show_error(
                f"Transferencia: error al escribir fragmento de '{sender}': {e}"
            )
            return

        self._net.send(protocol.build_transfer_ack_chunk(sender))

        if file_recv.is_complete:
            file_recv.close()
            self._chat_view.show_system(
                f"'{file_recv.filename}' guardado en '{DOWNLOAD_DIR}/'."
            )
            del self._file_recvs[sender]

    # ── Callbacks de transferencia ────────────────────────────────────────

    def _on_transfer_done(self):
        self._chat_view.set_transfer_active(False)
        self._chat_view.show_system("Archivo enviado correctamente.")
        self._file_ctrl = None

    def _on_transfer_error(self, msg: str):
        self._chat_view.set_transfer_active(False)
        self._chat_view.show_error(f"Transferencia: {msg}")
        self._file_ctrl = None

    def _on_login_ok(self):
        self._show_chat()
        # Nos agregamos al sidebar como "(tú)"
        self._chat_view.add_user(self._username, is_self=True)

    def _on_disconnected(self):
        """Limpia el estado y vuelve al Login View."""
        self._net.disconnect()
        if self._file_ctrl:
            self._file_ctrl.abort()
            self._file_ctrl = None
        for recv in self._file_recvs.values():
            recv.close()
        self._file_recvs.clear()
        self._chat_view.clear_users()
        self._login_view.reset()
        self._login_view.set_status("Desconectado del servidor.", error=True)
        self._show_login()
