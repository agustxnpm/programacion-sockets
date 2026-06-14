"""
Vista de Login.

Muestra un card centrado con:
  - Input para el nombre de usuario (máx. 20 caracteres).
  - Botón Conectar (deshabilitado mientras se espera el ACK del servidor).
  - Label de estado para feedback de error o progreso.

La transición al Chat View se activa ÚNICAMENTE desde ChatApp cuando el
hilo listener recibe el OpCode 0x07 con sub-código 0x03 (Login Exitoso).
"""
import customtkinter as ctk


class LoginView(ctk.CTkFrame):
    def __init__(self, master, on_connect):
        """
        on_connect(name: str, host: str) — callback llamado al pulsar Conectar.
        Se llama solo si el nombre es válido; la vista pasa a estado "conectando".
        """
        super().__init__(master, fg_color="transparent")
        self._on_connect = on_connect
        self._build()

    def _build(self):
        # Celda expansiva para centrar el card
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)

        card = ctk.CTkFrame(self, corner_radius=16)
        card.grid(row=0, column=0)        # sin sticky → centrado en la celda
        card.columnconfigure(0, weight=1)

        ctk.CTkLabel(
            card, text="Chat",
            font=ctk.CTkFont(size=28, weight="bold")
        ).grid(row=0, column=0, pady=(40, 6), padx=50)

        ctk.CTkLabel(
            card, text="Ingresá tu nombre para conectarte",
            font=ctk.CTkFont(size=12), text_color="gray"
        ).grid(row=1, column=0, pady=(0, 26), padx=50)

        self._name_entry = ctk.CTkEntry(
            card, placeholder_text="Nombre de usuario",
            width=260, height=44, font=ctk.CTkFont(size=14)
        )
        self._name_entry.grid(row=2, column=0, padx=40, pady=(0, 12))

        self._ip_entry = ctk.CTkEntry(
            card, placeholder_text="IP del servidor (ej: 192.168.1.10)",
            width=260, height=44, font=ctk.CTkFont(size=14)
        )
        self._ip_entry.grid(row=3, column=0, padx=40, pady=(0, 12))
        self._ip_entry.insert(0, "127.0.0.1")

        self._name_entry.bind('<Return>', lambda _: self._handle_connect())
        self._ip_entry.bind('<Return>', lambda _: self._handle_connect())
        self._name_entry.focus()

        self._connect_btn = ctk.CTkButton(
            card, text="Conectar", width=260, height=44,
            font=ctk.CTkFont(size=14, weight="bold"),
            command=self._handle_connect
        )
        self._connect_btn.grid(row=4, column=0, padx=40, pady=(0, 12))

        self._status = ctk.CTkLabel(
            card, text="",
            font=ctk.CTkFont(size=12), text_color="gray"
        )
        self._status.grid(row=5, column=0, pady=(0, 36), padx=50)

    # ── Handlers internos ─────────────────────────────────────────────────

    def _handle_connect(self):
        name = self._name_entry.get().strip()
        host = self._ip_entry.get().strip()
        if not name:
            self.set_status("Ingresá un nombre de usuario.", error=True)
            return
        if len(name) > 20:
            self.set_status("El nombre no puede superar los 20 caracteres.", error=True)
            return
        if not host:
            self.set_status("Ingresá la IP del servidor.", error=True)
            return
        self.set_connecting(True)
        self._on_connect(name, host)

    # ── API pública ───────────────────────────────────────────────────────

    def set_connecting(self, active: bool):
        """Deshabilita/habilita controles durante el handshake de login."""
        state = "disabled" if active else "normal"
        self._connect_btn.configure(state=state)
        self._name_entry.configure(state=state)
        self._ip_entry.configure(state=state)
        if active:
            self.set_status("Conectando...")

    def set_status(self, text: str, error: bool = False):
        self._status.configure(
            text=text,
            text_color="#E74C3C" if error else "gray"
        )

    def reset(self):
        """Restaura la vista a su estado inicial (tras una desconexión)."""
        self._name_entry.configure(state="normal")
        self._ip_entry.configure(state="normal")
        self._connect_btn.configure(state="normal")
        self._name_entry.delete(0, 'end')
        self._ip_entry.delete(0, 'end')
        self._ip_entry.insert(0, "127.0.0.1")
        self.set_status("")
