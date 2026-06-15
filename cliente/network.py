"""
Capa de transporte — gestiona el socket TCP y el hilo listener daemon.

Responsabilidades:
  - Conectar y desconectar el socket.
  - Proveer un único punto de escritura thread-safe (send).
  - Correr un hilo daemon que lee cabecera + payload y llama on_packet.

El caller (ChatApp) es responsable de delegar on_packet al hilo GUI
usando root.after(0, ...) antes de pasarlo aquí.
"""
import socket
import threading
import time
from typing import Callable

import protocol


class NetworkClient:
    def __init__(self, on_packet: Callable[[int, bytes], None]):
        """
        on_packet(opcode, payload) se invoca desde el hilo listener cada vez
        que llega un paquete completo.
        opcode == -1 es una señal sintética de desconexión inesperada.
        """
        self._on_packet = on_packet
        self._sock: socket.socket | None = None
        self._send_lock = threading.Lock()
        self._connected = False

    # ── Conexión ──────────────────────────────────────────────────────────

    def connect(self, host: str, port: int) -> None:
        """Establece la conexión TCP y lanza el hilo listener daemon."""
        self._sock = socket.create_connection((host, port), timeout=5.0)
        self._sock.settimeout(None) # Restablecer a bloqueante puro
        self._connected = True
        threading.Thread(target=self._listener_loop, daemon=True).start()
        self._start_heartbeat()

    def disconnect(self) -> None:
        """Cierra el socket limpiamente. Seguro de llamar varias veces."""
        self._connected = False
        if self._sock:
            try:
                self._sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    # ── Heartbeat ──────────────────────────────────────────────────────────

    def _start_heartbeat(self) -> None:
        """
        Envía un paquete con opcode 0xFF (ignorado por el servidor) cada 8 s.
        Esto resetea el SO_RCVTIMEO del servidor y evita que kick al cliente
        por inactividad durante la sesión.
        """
        def _beat():
            while self._connected:
                time.sleep(8)
                if self._connected:
                    try:
                        self.send(protocol.build_packet(0xFF, b''))
                    except OSError:
                        break
        threading.Thread(target=_beat, daemon=True).start()

    # ── Escritura ─────────────────────────────────────────────────────────

    def send(self, data: bytes) -> None:
        """
        Envía datos al servidor de forma thread-safe.
        Puede ser llamado desde el hilo GUI o desde el hilo de transferencia.
        """
        with self._send_lock:
            if self._sock and self._connected:
                self._sock.sendall(data)

    # ── Hilo listener (daemon) ────────────────────────────────────────────

    def _recv_exact(self, n: int) -> bytes:
        """Lee exactamente n bytes del socket (mitiga Short Counts)."""
        buf = b''
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("Conexión cerrada por el servidor")
            buf += chunk
        return buf

    def _listener_loop(self) -> None:
        """
        Bucle bloqueante que corre en un hilo daemon.
        Lee cabecera → payload → llama on_packet.
        Al detectar desconexión emite on_packet(-1, b'').
        """
        try:
            while self._connected:
                header          = self._recv_exact(protocol.HEADER_SIZE)
                opcode, length  = protocol.parse_header(header)
                payload         = self._recv_exact(length) if length > 0 else b''
                self._on_packet(opcode, payload)
        except (ConnectionError, OSError):
            if self._connected:
                self._connected = False
                self._on_packet(-1, b'')   # señal de desconexión inesperada
