"""
Controladores de transferencia de archivos.

FileTransferController — emisor (Stop-and-Wait con handshake de consentimiento).
FileReceiver           — receptor (escribe chunks a disco, envía ACK por chunk).

Ambas clases corren en hilos daemon separados del hilo GUI.
Toda comunicación de vuelta a la GUI se hace exclusivamente vía root.after(0, ...).
"""
import os
import threading
from pathlib import Path
from typing import Callable

import protocol


CONSENT_TIMEOUT_SEC = 30
CHUNK_ACK_TIMEOUT_SEC = 30


class FileTransferController:
    """
    Maneja el envío de un archivo en el lado del emisor.

    Flujo Stop-and-Wait:
      1. Validar tamaño local (< 100 MB).
      2. Enviar aviso 0x03 (dest, size, filename).
      3. Esperar consentimiento 0x07/0x02 (timeout 10 s).
      4. Si aceptado: loop de chunks → enviar 0x04 → esperar ACK 0x07/0x01.
      5. Llamar on_done() al terminar o on_error(msg) ante cualquier fallo.
    """

    def __init__(self, net, dest: str, filepath: Path, root, callbacks: dict):
        self._net       = net
        self._dest      = dest
        self._filepath  = filepath
        self._root      = root
        self._on_done:     Callable           = callbacks.get('on_done',     lambda:    None)
        self._on_error:    Callable[[str], None] = callbacks.get('on_error', lambda _:  None)
        self._on_progress: Callable[[float], None] = callbacks.get('on_progress', lambda _: None)

        self._consent_event   = threading.Event()
        self._chunk_ack_event = threading.Event()
        self._consent_accepted = False
        self._aborted = False

    # ── API pública ───────────────────────────────────────────────────────

    def start(self) -> None:
        threading.Thread(target=self._run, daemon=True).start()

    def notify_consent(self, accepted: bool) -> None:
        """Llamado desde el hilo GUI cuando llega el 0x07/0x02."""
        self._consent_accepted = accepted
        self._consent_event.set()

    def notify_chunk_ack(self) -> None:
        """Llamado desde el hilo GUI cuando llega el 0x07/0x01."""
        self._chunk_ack_event.set()

    def abort(self) -> None:
        """
        Cancela la transferencia desde cualquier hilo.
        Desbloquea los Event.wait() pendientes para que el hilo termine limpio.
        """
        self._aborted = True
        self._consent_event.set()
        self._chunk_ack_event.set()

    # ── Hilo de transferencia ─────────────────────────────────────────────

    def _run(self) -> None:
        file_size = os.path.getsize(self._filepath)

        # 1. Validación local de tamaño
        if file_size > protocol.MAX_FILE_SIZE:
            self._root.after(0, self._on_error, "El archivo supera el límite de 100 MB.")
            return

        # 2. Aviso al destinatario (0x03)
        self._net.send(
            protocol.build_file_notice(self._dest, file_size, self._filepath.name)
        )

        # 3. Esperar consentimiento
        if not self._consent_event.wait(timeout=CONSENT_TIMEOUT_SEC) or self._aborted:
            if not self._aborted:
                self._root.after(0, self._on_error, "Timeout: el destinatario no respondió.")
            return

        if not self._consent_accepted:
            self._root.after(0, self._on_error, "El destinatario rechazó el archivo.")
            return

        # 4. Envío de chunks (Stop-and-Wait)
        bytes_sent = 0
        with open(self._filepath, 'rb') as f:
            while not self._aborted:
                chunk = f.read(protocol.MAX_CHUNK_SIZE)
                if not chunk:
                    break

                self._chunk_ack_event.clear()
                self._net.send(protocol.build_file_chunk(self._dest, chunk))

                if not self._chunk_ack_event.wait(timeout=CHUNK_ACK_TIMEOUT_SEC) or self._aborted:
                    if not self._aborted:
                        self._root.after(
                            0, self._on_error,
                            "Timeout: el destinatario no confirmó el fragmento."
                        )
                    return

                bytes_sent += len(chunk)
                pct = (bytes_sent / file_size * 100) if file_size > 0 else 100.0
                self._root.after(0, self._on_progress, pct)

        if not self._aborted:
            self._root.after(0, self._on_done)


class FileReceiver:
    """
    Maneja la recepción de un archivo en el lado del receptor.

    Responsabilidad: abrir el archivo en disco y escribir chunks conforme llegan.
    El llamador (ChatApp) es responsable de enviar el ACK 0x07/0x01 tras cada chunk.
    """

    def __init__(self, filename: str, total_size: int, download_dir: Path):
        download_dir.mkdir(parents=True, exist_ok=True)
        base_path = download_dir / filename
        if base_path.exists():
            stem = base_path.stem
            suffix = base_path.suffix
            n = 1
            while True:
                candidate = download_dir / f"{stem} ({n}){suffix}"
                if not candidate.exists():
                    base_path = candidate
                    break
                n += 1

        self._path           = base_path
        self._total_size     = total_size
        self._bytes_received = 0
        self._file           = open(self._path, 'wb')

    def write_chunk(self, data: bytes) -> float:
        """Escribe un chunk en disco. Retorna el progreso de 0 a 100."""
        self._file.write(data)
        self._bytes_received += len(data)
        if self._total_size > 0:
            return min(self._bytes_received / self._total_size * 100, 100.0)
        return 100.0

    @property
    def is_complete(self) -> bool:
        return self._bytes_received >= self._total_size

    @property
    def filename(self) -> str:
        return self._path.name

    def close(self) -> None:
        self._file.close()
