"""
Diálogo modal de consentimiento para archivos entrantes.

Se muestra cuando el hilo listener recibe un OpCode 0x03 (aviso de archivo).
Bloquea la ventana principal hasta que el usuario acepte o rechace.

on_accept() y on_reject() son llamados en el hilo GUI (el diálogo corre en él).
"""
import customtkinter as ctk
from typing import Callable


class FileConsentDialog(ctk.CTkToplevel):
    def __init__(self, parent, filename: str, size: int,
                 on_accept: Callable, on_reject: Callable):
        super().__init__(parent)
        self.title("Archivo entrante")
        self.resizable(False, False)

        # Centrar sobre la ventana padre una vez que el diálogo tenga tamaño real
        self.update_idletasks()
        dlg_w, dlg_h = 360, 210
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

        self._on_accept = on_accept
        self._on_reject = on_reject

        self.columnconfigure(0, weight=1)

        ctk.CTkLabel(
            self, text="Archivo entrante",
            font=ctk.CTkFont(size=16, weight="bold")
        ).grid(row=0, column=0, pady=(26, 6))

        ctk.CTkLabel(
            self, text=filename,
            font=ctk.CTkFont(size=13), text_color="#7EB8F7"
        ).grid(row=1, column=0, pady=(0, 4))

        if size >= 1024 * 1024:
            size_str = f"{size / (1024 * 1024):.2f} MB"
        else:
            size_str = f"{size / 1024:.1f} KB"

        ctk.CTkLabel(
            self, text=size_str,
            font=ctk.CTkFont(size=12), text_color="gray"
        ).grid(row=2, column=0, pady=(0, 22))

        btn_frame = ctk.CTkFrame(self, fg_color="transparent")
        btn_frame.grid(row=3, column=0, pady=(0, 26))

        ctk.CTkButton(
            btn_frame, text="Aceptar", width=120, height=38,
            command=self._accept
        ).pack(side="left", padx=8)

        ctk.CTkButton(
            btn_frame, text="Rechazar", width=120, height=38,
            fg_color="#C0392B", hover_color="#922B21",
            command=self._reject
        ).pack(side="left", padx=8)

    def _accept(self):
        self._on_accept()
        self.destroy()

    def _reject(self):
        self._on_reject()
        self.destroy()
