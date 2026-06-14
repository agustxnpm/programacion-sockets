"""
Diálogo modal de consentimiento para archivos entrantes.

Se muestra cuando el hilo listener recibe un OpCode 0x03 (aviso de archivo).
Bloquea la ventana principal hasta que el usuario acepte o rechace.

on_accept() y on_reject() son llamados en el hilo GUI (el diálogo corre en él).
Si el usuario no responde en CONSENT_TIMEOUT_SEC segundos, se llama on_timeout()
(o on_reject() si no se proveyó on_timeout).
"""
import customtkinter as ctk
from typing import Callable

CONSENT_TIMEOUT_SEC = 30


class FileConsentDialog(ctk.CTkToplevel):
    def __init__(self, parent, filename: str, size: int, sender: str,
                 on_accept: Callable, on_reject: Callable,
                 on_timeout: Callable | None = None):
        super().__init__(parent)
        self.title("Archivo entrante")
        self.resizable(False, False)

        # Centrar sobre la ventana padre una vez que el diálogo tenga tamaño real
        self.update_idletasks()
        dlg_w, dlg_h = 360, 250
        px = parent.winfo_rootx()
        py = parent.winfo_rooty()
        pw = parent.winfo_width()
        ph = parent.winfo_height()
        x = px + (pw - dlg_w) // 2
        y = py + (ph - dlg_h) // 2
        self.geometry(f"{dlg_w}x{dlg_h}+{x}+{y}")

        self.grab_set()      # modal: bloquea la ventana padre
        self.lift()
        self.focus_force()

        self._on_accept  = on_accept
        self._on_reject  = on_reject
        self._on_timeout = on_timeout if on_timeout is not None else on_reject
        self._closed     = False
        self._remaining  = CONSENT_TIMEOUT_SEC

        self.columnconfigure(0, weight=1)

        ctk.CTkLabel(
            self, text="Archivo entrante",
            font=ctk.CTkFont(size=16, weight="bold")
        ).grid(row=0, column=0, pady=(20, 4))

        ctk.CTkLabel(
            self, text=f"De: {sender}",
            font=ctk.CTkFont(size=12), text_color="#AAAACC"
        ).grid(row=1, column=0, pady=(0, 2))

        ctk.CTkLabel(
            self, text=filename,
            font=ctk.CTkFont(size=13), text_color="#7EB8F7"
        ).grid(row=2, column=0, pady=(0, 2))

        if size >= 1024 * 1024:
            size_str = f"{size / (1024 * 1024):.2f} MB"
        else:
            size_str = f"{size / 1024:.1f} KB"

        ctk.CTkLabel(
            self, text=size_str,
            font=ctk.CTkFont(size=12), text_color="gray"
        ).grid(row=3, column=0, pady=(0, 6))

        self._countdown_label = ctk.CTkLabel(
            self, text=f"Se cerrará en {self._remaining}s",
            font=ctk.CTkFont(size=11), text_color="#555570"
        )
        self._countdown_label.grid(row=4, column=0, pady=(0, 12))

        btn_frame = ctk.CTkFrame(self, fg_color="transparent")
        btn_frame.grid(row=5, column=0, pady=(0, 20))

        ctk.CTkButton(
            btn_frame, text="Aceptar", width=120, height=38,
            command=self._accept
        ).pack(side="left", padx=8)

        ctk.CTkButton(
            btn_frame, text="Rechazar", width=120, height=38,
            fg_color="#C0392B", hover_color="#922B21",
            command=self._reject
        ).pack(side="left", padx=8)

        self._tick()

    # ── Cuenta regresiva ──────────────────────────────────────────────────

    def _tick(self):
        if self._closed:
            return
        if self._remaining <= 0:
            self._fire_timeout()
            return
        self._countdown_label.configure(text=f"Se cerrará en {self._remaining}s")
        self._remaining -= 1
        self.after(1000, self._tick)

    def _fire_timeout(self):
        if self._closed:
            return
        self._closed = True
        self._on_timeout()
        self.destroy()

    # ── Botones ───────────────────────────────────────────────────────────

    def _accept(self):
        if self._closed:
            return
        self._closed = True
        self._on_accept()
        self.destroy()

    def _reject(self):
        if self._closed:
            return
        self._closed = True
        self._on_reject()
        self.destroy()
