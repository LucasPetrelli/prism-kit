"""Protocol wire-format helpers and loopback test for prism-kit.

Wire format (matches ``protocol::Frame`` in ``protocol/include/frame.hpp``):
  sync(1) | tag(2 LE) | length(2 LE) | data(N) | checksum(1)

- Sync byte: 0xAA (``protocol::kSyncByte``)
- Loopback tag: 0x0000 (``protocol::Tag::kLoopback``)
- SetSingleColor tag: 0x0101 (``protocol::Tag::kSetSingleColor``)
- ResetInstructions tag: 0x0102 (``protocol::Tag::kResetInstructions``)
- Run tag: 0x0103 (``protocol::Tag::kRun``)
- Checksum: XOR of tag + length + data bytes.
"""

from __future__ import annotations

import sys
import time
from typing import Any

SYNC_BYTE = 0xAA
TAG_LOOPBACK = 0x0000
TAG_SET_SINGLE_COLOR = 0x0101
TAG_RESET_INSTRUCTIONS = 0x0102
TAG_RUN = 0x0103


def build_frame(tag: int, payload: bytes) -> bytes:
    """Build a complete protocol wire frame for the given *tag* and *payload*.

    Wire format: sync(1) | tag(2 LE) | length(2 LE) | data(N) | checksum(1)
    Checksum is XOR of tag + length + data bytes.
    """
    length = len(payload)
    header = tag.to_bytes(2, "little") + length.to_bytes(2, "little")

    cs = 0
    for b in header:
        cs ^= b
    for b in payload:
        cs ^= b

    return bytes([SYNC_BYTE]) + header + payload + bytes([cs])


def build_loopback_frame(payload: bytes) -> bytes:
    """Build a complete protocol wire frame for the loopback tag.

    Delegates to :func:`build_frame` with ``TAG_LOOPBACK``.
    """
    return build_frame(TAG_LOOPBACK, payload)


def build_set_single_color_frame(r: int, g: int, b: int, index: int) -> bytes:
    """Build a ``kSetSingleColor`` frame.

    Payload: ``r(1) g(1) b(1) index(1)`` (4 bytes).

    Sends a single-pixel colour command to the prism-kit firmware.  The
    instruction is queued in the controller but not executed until a
    subsequent ``kRun`` frame is sent.
    """
    return build_frame(TAG_SET_SINGLE_COLOR, bytes([r, g, b, index]))


def build_reset_instructions_frame() -> bytes:
    """Build a ``kResetInstructions`` frame (0-byte payload).

    Clears the prism-kit controller's instruction queue without altering
    the current pixel state on the strip.
    """
    return build_frame(TAG_RESET_INSTRUCTIONS, b"")


def build_run_frame() -> bytes:
    """Build a ``kRun`` frame (0-byte payload).

    Instructs the prism-kit controller to execute all queued instructions
    and flush the resulting pixel colours to the physical LED strip.
    """
    return build_frame(TAG_RUN, b"")


def parse_wire_frame(data: bytes) -> dict | None:
    """Try to parse a single protocol wire frame from *data*.

    Returns a dict with keys ``tag``, ``payload``, ``checksum_ok`` on success,
    or *None* if the frame is too short or the sync byte is wrong.
    """
    if len(data) < 8:
        return None
    if data[0] != SYNC_BYTE:
        return None

    tag = int.from_bytes(data[1:3], "little")
    length = int.from_bytes(data[3:5], "little")

    frame_size = 5 + length + 1  # sync + header + data + checksum
    if len(data) < frame_size:
        return None

    payload = data[5 : 5 + length]
    received_cs = data[5 + length]

    expected_cs = 0
    for b in data[1 : 5 + length]:
        expected_cs ^= b

    return {
        "tag": tag,
        "payload": payload,
        "checksum_ok": received_cs == expected_cs,
    }


def run_loopback_test(
    serial_module: Any,
    command_device: str,
    baudrate: int,
    payload: bytes,
    timeout: float = 5.0,
) -> bool:
    """Send a loopback frame and verify the echo response.

    Returns *True* on success, prints diagnostic information to stderr and
    returns *False* on failure.
    """
    frame = build_loopback_frame(payload)

    with serial_module.Serial(
        command_device, baudrate=baudrate, timeout=0.1, write_timeout=1.0
    ) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()
        port.write(frame)

        deadline = time.monotonic() + timeout
        accumulated = bytearray()

        while time.monotonic() < deadline:
            chunk = port.read(64)
            if chunk:
                accumulated.extend(chunk)

            result = parse_wire_frame(bytes(accumulated))
            if result is not None:
                break
        else:
            # Loop exhausted without finding a valid frame.
            print(
                f"Loopback FAIL — no valid frame received within {timeout:.1f}s.",
                file=sys.stderr,
            )
            if accumulated:
                print(f"  Raw bytes received: {accumulated.hex()}", file=sys.stderr)
            return False

    # ── Validate the received frame (early-return chain) ────────────────
    if result["tag"] != TAG_LOOPBACK:
        print(
            f"Loopback FAIL — unexpected tag 0x{result['tag']:04X} "
            f"(expected 0x{TAG_LOOPBACK:04X}).",
            file=sys.stderr,
        )
        return False

    if not result["checksum_ok"]:
        print("Loopback FAIL — checksum mismatch on received frame.", file=sys.stderr)
        return False

    if result["payload"] != payload:
        print(
            f"Loopback FAIL — payload mismatch. "
            f"Sent {payload.hex()}, received {result['payload'].hex()}.",
            file=sys.stderr,
        )
        return False

    print("Loopback OK")
    return True
