"""
Vista de Chat principal.

Layout:
  ┌─ Sidebar (160px) ─┬─────────── Chat ───────────────────────┐
  │ Conectados        │ Header (Chat · usuario)                 │
  │  * Alice          ├─────── Mensajes (expansible) ──────────┤
  │  * Bob            │ ...                                     │
  │                   ├─────── Barra transferencia (oculta) ────┤
  │                   ├─────── Input ──────────────────────────┤
  │                   │ [Mensaje / @usuario msg] [Enviar] [+]  │
  └───────────────────┴─────────────────────────────────────────┘

Reglas de uso:
  - Mensaje sin @: difusión a todos.
  - Mensaje con @usuario texto: privado al usuario indicado.
  - Clic en un usuario del sidebar: autocompleta "@usuario " en el input.
  - Botón +: el input debe comenzar con @usuario para adjuntar archivo.

La vista no contiene lógica de red ni de protocolo.
"""
import customtkinter as ctk
from tkinter import filedialog
from datetime import datetime


class ChatView(ctk.CTkFrame):
    def __init__(self, master, on_send_private, on_send_broadcast, on_send_file,
                 on_cancel_send=None, on_cancel_recv=None):
        """
        on_send_private(dest, text)  — mensaje privado.
        on_send_broadcast(text)      — difusión a todos.
        on_send_file(dest, filepath) — envío de archivo.
        on_cancel_send()             — cancela el envío activo.
        on_cancel_recv(sender)       — cancela la recepción activa de un emisor.
        """
        super().__init__(master, fg_color="transparent")
        self._on_send_private   = on_send_private
        self._on_send_broadcast = on_send_broadcast
        self._on_send_file      = on_send_file
        self._on_cancel_send    = on_cancel_send or (lambda: None)
        self._on_cancel_recv    = on_cancel_recv or (lambda _sender: None)
        self._user_buttons: dict[str, ctk.CTkButton] = {}
        self._recv_widgets: dict[str, dict] = {}
        self._build()

    # ── Construcción ──────────────────────────────────────────────────────

    def _build(self):
        # Columna 0: sidebar fija · Columna 1: chat expansible
        self.columnconfigure(1, weight=1)
        self.rowconfigure(0, weight=1)

        self._build_sidebar()
        self._build_main()

    def _build_sidebar(self):
        sidebar = ctk.CTkFrame(self, width=160, corner_radius=0)
        sidebar.grid(row=0, column=0, sticky="nsew")
        sidebar.rowconfigure(2, weight=1)
        sidebar.columnconfigure(0, weight=1)
        sidebar.grid_propagate(False)

        ctk.CTkLabel(
            sidebar, text="Conectados",
            font=ctk.CTkFont(size=11, weight="bold"), text_color="#666680"
        ).grid(row=0, column=0, padx=12, pady=(14, 6), sticky="w")

        # Canal de difusión (reemplaza al botón propio del usuario)
        ctk.CTkButton(
            sidebar,
            text="# General",
            anchor="w", height=30,
            fg_color="#2A2A3A", hover_color="#3A3A4A",
            text_color="#7EB8F7",
            font=ctk.CTkFont(size=12),
            command=self._fill_broadcast,
        ).grid(row=1, column=0, padx=4, pady=(0, 6), sticky="ew")

        self._users_frame = ctk.CTkScrollableFrame(
            sidebar, fg_color="transparent", corner_radius=0
        )
        self._users_frame.grid(row=2, column=0, sticky="nsew")
        self._users_frame.columnconfigure(0, weight=1)

    def _build_main(self):
        main = ctk.CTkFrame(self, corner_radius=0, fg_color="transparent")
        main.grid(row=0, column=1, sticky="nsew")
        main.columnconfigure(0, weight=1)
        main.rowconfigure(1, weight=1)

        # — Header ────────────────────────────────────────────────────────
        header = ctk.CTkFrame(main, height=52, corner_radius=0)
        header.grid(row=0, column=0, sticky="ew")
        header.columnconfigure(0, weight=1)

        self._header_label = ctk.CTkLabel(
            header, text="Chat",
            font=ctk.CTkFont(size=15, weight="bold"), anchor="w"
        )
        self._header_label.grid(row=0, column=0, padx=20, pady=14, sticky="w")

        ctk.CTkLabel(
            header, text="@usuario mensaje  →  privado  |  sin @  →  todos",
            font=ctk.CTkFont(size=11), text_color="#44445A", anchor="e"
        ).grid(row=0, column=1, padx=16, sticky="e")

        # — Área de mensajes ──────────────────────────────────────────────
        self._chat_box = ctk.CTkTextbox(
            main, state="disabled",
            font=ctk.CTkFont(size=13),
            corner_radius=0, wrap="word", border_width=0
        )
        self._chat_box.grid(row=1, column=0, sticky="nsew")

        # Los colores se aplican en _append para garantizar que no sean pisados
        # por CTkTextbox al reinicializar su widget interno.

        # — Barra de envío (oculta por defecto) ────────────────────────────
        self._transfer_frame = ctk.CTkFrame(main, height=34, corner_radius=0)
        self._transfer_frame.columnconfigure(1, weight=1)
        self._transfer_label = ctk.CTkLabel(
            self._transfer_frame, text="",
            font=ctk.CTkFont(size=11), text_color="gray", anchor="w"
        )
        self._transfer_label.grid(row=0, column=0, padx=(14, 8), sticky="w", pady=6)
        self._transfer_bar = ctk.CTkProgressBar(self._transfer_frame, height=10)
        self._transfer_bar.set(0)
        self._transfer_bar.grid(row=0, column=1, padx=(0, 8), sticky="ew", pady=10)
        ctk.CTkButton(
            self._transfer_frame, text="✕", width=32, height=22,
            fg_color="#C0392B", hover_color="#922B21",
            font=ctk.CTkFont(size=11),
            command=lambda: self._on_cancel_send(),
        ).grid(row=0, column=2, padx=(0, 10), pady=6)

        # — Barras de recepción (una por transferencia activa) ────────────
        self._recv_container = ctk.CTkFrame(main, corner_radius=0, fg_color="transparent")
        self._recv_container.grid(row=3, column=0, sticky="ew")
        self._recv_container.columnconfigure(0, weight=1)

        # — Input ─────────────────────────────────────────────────────────
        input_frame = ctk.CTkFrame(main, height=62, corner_radius=0)
        input_frame.grid(row=4, column=0, sticky="ew")
        input_frame.columnconfigure(0, weight=1)

        self._msg_entry = ctk.CTkEntry(
            input_frame,
            placeholder_text="Mensaje a todos  ·  @usuario mensaje para privado...",
            height=38, font=ctk.CTkFont(size=13)
        )
        self._msg_entry.grid(row=0, column=0, padx=(12, 8), pady=12, sticky="ew")
        self._msg_entry.bind('<Return>', lambda _: self._handle_send())

        self._send_btn = ctk.CTkButton(
            input_frame, text="Enviar", width=76, height=38,
            font=ctk.CTkFont(size=13, weight="bold"),
            command=self._handle_send
        )
        self._send_btn.grid(row=0, column=1, padx=(0, 6), pady=12)

        self._file_btn = ctk.CTkButton(
            input_frame, text="📎", width=42, height=38,
            fg_color="#3A3A4A", hover_color="#4A4A5A",
            font=ctk.CTkFont(size=16),
            command=self._handle_file_pick
        )
        self._file_btn.grid(row=0, column=2, padx=(0, 12), pady=12)

    # ── Handlers de input ─────────────────────────────────────────────────

    def _handle_send(self):
        text = self._msg_entry.get().strip()
        if not text:
            return

        if text.startswith('@'):
            # Privado: @usuario mensaje
            parts = text.split(' ', 1)
            if len(parts) < 2 or not parts[1].strip():
                self._append("[!] Formato: @usuario mensaje", tag="error")
                return
            dest = parts[0][1:]
            msg  = parts[1].strip()
            self._on_send_private(dest, msg)
        else:
            # Difusión por defecto
            self._on_send_broadcast(text)

        self._msg_entry.delete(0, 'end')

    def _handle_file_pick(self):
        text = self._msg_entry.get().strip()
        if not text.startswith('@'):
            self._append(
                "[!] Para enviar un archivo escribí @usuario en el campo de mensaje.",
                tag="error"
            )
            return
        dest = text.split()[0][1:]
        filepath = filedialog.askopenfilename(title="Seleccionar archivo para enviar")
        if filepath:
            self._on_send_file(dest, filepath)

    # ── Sidebar: gestión de usuarios ─────────────────────────────────────

    def add_user(self, name: str, is_self: bool = False):
        """Agrega un usuario al panel lateral. Idempotente.
        El propio usuario (is_self=True) no se agrega: el botón General lo reemplaza.
        """
        if is_self:
            return
        if name in self._user_buttons:
            return
        btn = ctk.CTkButton(
            self._users_frame,
            text=f"* {name}",
            anchor="w", height=30,
            fg_color="transparent", hover_color="#2A2A3A",
            text_color="#AAAACC",
            font=ctk.CTkFont(size=12),
            command=lambda n=name: self._fill_private(n),
        )
        btn.grid(sticky="ew", padx=4, pady=2)
        self._user_buttons[name] = btn

    def remove_user(self, name: str):
        btn = self._user_buttons.pop(name, None)
        if btn:
            btn.destroy()

    def clear_users(self):
        for btn in self._user_buttons.values():
            btn.destroy()
        self._user_buttons.clear()

    def _fill_broadcast(self):
        """Limpia cualquier @mention del input para volver al modo difusión (General)."""
        current = self._msg_entry.get()
        if current.startswith('@'):
            parts = current.split(' ', 1)
            rest  = parts[1] if len(parts) > 1 else ""
            self._msg_entry.delete(0, 'end')
            self._msg_entry.insert(0, rest.strip())
        self._msg_entry.focus()

    def _fill_private(self, username: str):
        """Al hacer clic en un usuario, autocompleta '@usuario ' en el input."""
        current = self._msg_entry.get()
        if current.startswith('@'):
            # Reemplaza solo el mention, conserva el texto escrito
            parts = current.split(' ', 1)
            rest  = parts[1] if len(parts) > 1 else ""
            new   = f"@{username} {rest}"
        else:
            new = f"@{username} {current}"
        self._msg_entry.delete(0, 'end')
        self._msg_entry.insert(0, new)
        self._msg_entry.focus()
        self._msg_entry.icursor('end')

    # ── API de mensajes ───────────────────────────────────────────────────

    def show_broadcast(self, text: str):
        """Mensaje de difusión recibido (ya incluye '[emisor]: ' si viene de un cliente)."""
        self._append(text, tag="broadcast")

    def show_private(self, text: str):
        """
        Mensaje privado recibido.
        Muestra el badge '[privado]' y lo resalta en azul.
        """
        self._append(f"[privado]  {text}", tag="private")

    def show_private_sent(self, dest: str, text: str):
        """Eco local del mensaje privado que nosotros enviamos."""
        self._append(f"→ @{dest}: {text}", tag="self_out")

    def show_broadcast_sent(self, text: str):
        """Eco local de la difusión que nosotros enviamos."""
        self._append(text, tag="self_out")

    def show_error(self, text: str):
        self._append(f"[!] {text}", tag="error")

    def show_system(self, text: str):
        self._append(text, tag="system")

    # ── Header / progreso ─────────────────────────────────────────────────

    def set_header(self, username: str):
        self._header_label.configure(text=f"Chat  ·  {username}")

    def set_transfer_active(self, active: bool, filename: str = "",
                            label_text: str = ""):
        if active:
            text = label_text if label_text else f"Enviando: {filename}"
            self._transfer_label.configure(text=text)
            self._transfer_bar.set(0)
            self._transfer_frame.grid(row=2, column=0, sticky="ew")
            self._send_btn.configure(state="disabled")
            self._file_btn.configure(state="disabled")
        else:
            self._transfer_frame.grid_forget()
            self._send_btn.configure(state="normal")
            self._file_btn.configure(state="normal")

    def update_transfer_progress(self, pct: float):
        self._transfer_bar.set(pct / 100)
        self._transfer_label.configure(text=f"Enviando: {pct:.0f}%")

    def set_recv_active(self, active: bool, filename: str = "", sender: str = ""):
        if not sender:
            return

        if active:
            if sender in self._recv_widgets:
                self._recv_widgets[sender]["base"] = (
                    f"Recibiendo: {filename} (de {sender})"
                )
                self._recv_widgets[sender]["label"].configure(
                    text=self._recv_widgets[sender]["base"]
                )
                self._recv_widgets[sender]["bar"].set(0)
                return

            frame = ctk.CTkFrame(self._recv_container, height=34, corner_radius=0)
            frame.columnconfigure(1, weight=1)
            frame.grid(sticky="ew", pady=2)

            base = f"Recibiendo: {filename} (de {sender})"
            label = ctk.CTkLabel(
                frame, text=base,
                font=ctk.CTkFont(size=11), text_color="gray", anchor="w"
            )
            label.grid(row=0, column=0, padx=(14, 8), sticky="w", pady=6)

            bar = ctk.CTkProgressBar(frame, height=10)
            bar.set(0)
            bar.grid(row=0, column=1, padx=(0, 8), sticky="ew", pady=10)

            ctk.CTkButton(
                frame, text="✕", width=32, height=22,
                fg_color="#C0392B", hover_color="#922B21",
                font=ctk.CTkFont(size=11),
                command=lambda s=sender: self._on_cancel_recv(s),
            ).grid(row=0, column=2, padx=(0, 10), pady=6)

            self._recv_widgets[sender] = {
                "frame": frame,
                "label": label,
                "bar": bar,
                "base": base,
            }
        else:
            widgets = self._recv_widgets.pop(sender, None)
            if widgets:
                widgets["frame"].destroy()

    def update_recv_progress(self, sender: str, pct: float):
        widgets = self._recv_widgets.get(sender)
        if not widgets:
            return
        widgets["bar"].set(pct / 100)
        widgets["label"].configure(text=f"{widgets['base']}  {pct:.0f}%")

    # ── Interno ───────────────────────────────────────────────────────────

    _TAG_COLORS = {
        "broadcast": "#D0D0D0",
        "private":   "#F9E79F",
        "self_out":  "#82C882",
        "system":    "#666680",
        "error":     "#E74C3C",
    }

    def _append(self, text: str, tag: str = "broadcast"):
        ts = datetime.now().strftime("%H:%M")
        tb = self._chat_box._textbox      # tkinter.Text subyacente
        # Re-aplicar el color siempre: CTkTextbox puede reinicializar el widget
        # interno y pisar configuraciones previas de tags.
        color = self._TAG_COLORS.get(tag, "#D0D0D0")
        tb.tag_configure(tag, foreground=color)
        tb.configure(state="normal")
        # 'end' en tkinter.Text apunta al salto final interno; para taggear
        # el texto real recién insertado hay que usar 'end-1c'.
        start = tb.index("end-1c")
        tb.insert("end", f"[{ts}] {text}\n")
        end = tb.index("end-1c")
        tb.tag_add(tag, start, end)
        tb.configure(state="disabled")
        tb.see("end")
