"""
Capa de serialización — funciones puras, sin estado, sin imports de red ni GUI.
Toda construcción y parseo de paquetes del protocolo TCP binario ocurre aquí.

Formato de cabecera: !BI  (1 byte OpCode + 4 bytes Payload Length, Big-Endian)
"""
import struct

HEADER_FORMAT = '!BI'
HEADER_SIZE   = struct.calcsize(HEADER_FORMAT)   # 5 bytes
MAX_CHUNK_SIZE    = 64 * 1024
MAX_FILE_SIZE     = 100 * 1024 * 1024            # 100 MB
MAX_USERNAME_LEN  = 20


# ── Primitivas ────────────────────────────────────────────────────────────────

def build_packet(opcode: int, payload: bytes) -> bytes:
    """Ensambla cabecera (5 bytes) + payload."""
    return struct.pack(HEADER_FORMAT, opcode, len(payload)) + payload


def parse_header(data: bytes) -> tuple:
    """Desempaqueta 5 bytes de cabecera. Retorna (opcode, payload_length)."""
    return struct.unpack(HEADER_FORMAT, data)


def _pad_dest(dest: str) -> bytes:
    """Codifica el nombre del destinatario en exactamente 20 bytes (relleno con ceros)."""
    encoded = dest.encode('utf-8')[:MAX_USERNAME_LEN]
    return encoded.ljust(MAX_USERNAME_LEN, b'\x00')


def encode_username_field(name: str) -> bytes:
    """Codifica un nombre de usuario en campo fijo de 20 bytes."""
    return _pad_dest(name)


# ── Constructores por OpCode ──────────────────────────────────────────────────

def build_login(name: str) -> bytes:
    """0x01 — Login: payload = nombre de usuario."""
    return build_packet(0x01, name.encode('utf-8'))


def build_private(dest: str, text: str) -> bytes:
    """0x02 — Mensaje privado: payload = dest(20b) + texto."""
    return build_packet(0x02, _pad_dest(dest) + text.encode('utf-8'))


def build_broadcast(text: str) -> bytes:
    """0x06 — Difusión: payload = texto."""
    return build_packet(0x06, text.encode('utf-8'))


def build_file_notice(dest: str, size: int, filename: str) -> bytes:
    """0x03 — Aviso de archivo: payload = dest(20b) + tamaño(8b big-endian) + nombre."""
    payload = _pad_dest(dest) + struct.pack('>Q', size) + filename.encode('utf-8')
    return build_packet(0x03, payload)


def build_file_chunk(dest: str, data: bytes) -> bytes:
    """0x04 — Fragmento de archivo: payload = dest(20b) + bytes del chunk (máx 64 KB)."""
    return build_packet(0x04, _pad_dest(dest) + data)


def build_ack(subcode: int, extra: bytes = b'') -> bytes:
    """
    0x07 — ACK Polimórfico:
      subcode 0x01          → ACK de fragmento (1 byte)
      subcode 0x02 + 0x01   → Consentimiento: aceptar
      subcode 0x02 + 0x00   → Consentimiento: rechazar
      subcode 0x03          → ACK de login exitoso (enviado por el servidor)
    """
    return build_packet(0x07, bytes([subcode]) + extra)


def build_transfer_ack_chunk(sender: str) -> bytes:
    """0x07/0x01 — ACK de chunk con emisor explícito para enrutado concurrente."""
    return build_ack(0x01, encode_username_field(sender))


def build_transfer_ack_consent(sender: str, accepted: bool) -> bytes:
    """0x07/0x02 — Consentimiento con emisor explícito para enrutado concurrente."""
    flag = b'\x01' if accepted else b'\x00'
    return build_ack(0x02, flag + encode_username_field(sender))


# ── Parsers ───────────────────────────────────────────────────────────────────

def parse_private(payload: bytes) -> str:
    """Extrae el texto de un mensaje privado recibido (los primeros 20 bytes son el destino)."""
    return payload[MAX_USERNAME_LEN:].decode('utf-8', errors='replace')


def parse_file_notice(payload: bytes) -> tuple:
    """
    Parsea el payload de un 0x03 recibido.
    Retorna (file_size: int, filename: str).
    """
    offset = MAX_USERNAME_LEN
    size = struct.unpack('>Q', payload[offset:offset + 8])[0]
    filename = payload[offset + 8:].decode('utf-8', errors='replace')
    return size, filename


def parse_transfer_peer(payload: bytes) -> str:
    """Extrae el campo de 20 bytes inicial (peer de transferencia) como texto."""
    return payload[:MAX_USERNAME_LEN].rstrip(b'\x00').decode('utf-8', errors='replace')
