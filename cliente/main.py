import customtkinter as ctk
import shutil
import atexit
from pathlib import Path
from app import ChatApp

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

# Usamos valores por defecto en los parámetros para asegurar que la ruta
# y 'shutil.rmtree' sigan existiendo en memoria durante el cierre del script.
def cleanup_temp_files(base_dir=Path(__file__).parent, rmtree=shutil.rmtree):
    for pycache in base_dir.rglob('__pycache__'):
        if pycache.is_dir():
            try:
                rmtree(pycache)
            except OSError:
                pass

atexit.register(cleanup_temp_files)

root = ctk.CTk()
ChatApp(root)
root.mainloop()
