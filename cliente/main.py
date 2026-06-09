import customtkinter as ctk
from app import ChatApp

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

root = ctk.CTk()
ChatApp(root)
root.mainloop()
