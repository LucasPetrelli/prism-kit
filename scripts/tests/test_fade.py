"""Host tests for the one-shot red fade protocol program."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import sys
from unittest import TestCase
from unittest.mock import patch

from scripts import fade
from scripts.modules import protocol


class FakePort:
    """Capture protocol writes like an open serial port."""

    def __init__(self, short_write_at: int | None = None) -> None:
        self.writes: list[bytes] = []
        self.short_write_at = short_write_at
        self.closed = False

    def __enter__(self) -> FakePort:
        """Return this port from a ``with`` block."""
        return self

    def __exit__(self, *_: object) -> None:
        """Record that the serial context has closed."""
        self.closed = True

    def reset_input_buffer(self) -> None:
        """Match the pyserial input-buffer API."""

    def reset_output_buffer(self) -> None:
        """Match the pyserial output-buffer API."""

    def write(self, data: bytes) -> int:
        """Record bytes and optionally simulate a short write."""
        self.writes.append(data)
        if len(self.writes) == self.short_write_at:
            return len(data) - 1
        return len(data)


class FakeSerialModule:
    """Provide fake ports without opening a serial device."""

    def __init__(self, short_write_at: int | None = None) -> None:
        self.ports: list[FakePort] = []
        self.short_write_at = short_write_at

    def Serial(self, *_: object, **__: object) -> FakePort:
        """Return a fresh capture port."""
        port = FakePort(self.short_write_at)
        self.ports.append(port)
        return port


def unpack_frame(frame: bytes) -> tuple[int, bytes]:
    """Decode one complete frame and verify its XOR checksum."""
    if frame[0] != protocol.SYNC_BYTE:
        raise AssertionError("missing frame sync byte")

    tag = int.from_bytes(frame[1:3], "little")
    payload_length = int.from_bytes(frame[3:5], "little")
    payload = frame[5 : 5 + payload_length]
    if len(frame) != payload_length + 6:
        raise AssertionError("frame length does not match payload")

    checksum = 0
    for byte in frame[1:-1]:
        checksum ^= byte
    if frame[-1] != checksum:
        raise AssertionError("frame checksum mismatch")
    return tag, payload


class FadeWireTest(TestCase):
    """Verify the complete fade program without serial hardware."""

    def test_programs_all_red_leds_with_five_second_fades(self) -> None:
        """Goal: program seven red pixels and a 10-second down/up fade.

        Steps: capture the serial frames, decode their checksums and payloads,
        then verify the full-range red setup, seven 25% fades at 0 ms, seven
        400% fades at 5,000 ms, and the single-run root start.
        """
        serial = FakeSerialModule()
        output = StringIO()

        with redirect_stdout(output):
            self.assertTrue(fade.run_fade_sequence(serial, "COM6", 115200))

        port = serial.ports[0]
        self.assertTrue(port.closed)
        self.assertEqual(len(port.writes), 17)
        decoded = [unpack_frame(frame) for frame in port.writes]
        self.assertEqual(
            [tag for tag, _ in decoded],
            [
                protocol.TAG_RESET_INSTRUCTIONS,
                protocol.TAG_SET_MULTIPLE_COLOR_GROUPED,
                *([protocol.TAG_FADE] * 14),
                protocol.TAG_START,
            ],
        )
        self.assertEqual(decoded[0], (protocol.TAG_RESET_INSTRUCTIONS, b""))
        self.assertEqual(
            decoded[1],
            (
                protocol.TAG_SET_MULTIPLE_COLOR_GROUPED,
                b"\x00\x00\xff\x00\x00\x00\x07\x00\x00\x00\x00",
            ),
        )

        for index, (tag, payload) in enumerate(decoded[2:9]):
            self.assertEqual(tag, protocol.TAG_FADE)
            self.assertEqual(int.from_bytes(payload[0:2], "little"), 0)
            self.assertEqual(int.from_bytes(payload[2:4], "little"), 25)
            self.assertEqual(int.from_bytes(payload[4:6], "little"), 5000)
            self.assertEqual(payload[6], index)
            self.assertEqual(payload[7:11], b"\x00\x00\x00\x00")

        for index, (tag, payload) in enumerate(decoded[9:16]):
            self.assertEqual(tag, protocol.TAG_FADE)
            self.assertEqual(int.from_bytes(payload[0:2], "little"), 5000)
            self.assertEqual(int.from_bytes(payload[2:4], "little"), 400)
            self.assertEqual(int.from_bytes(payload[4:6], "little"), 5000)
            self.assertEqual(payload[6], index)
            self.assertEqual(payload[7:11], b"\x00\x00\x00\x00")

        self.assertEqual(decoded[-1], (protocol.TAG_START, b"\x00\x00"))
        self.assertIn("cannot confirm controller acceptance", output.getvalue())

    def test_short_frame_write_fails_and_closes_the_port(self) -> None:
        """Goal: reject an incomplete instruction frame instead of reporting success.

        Steps: simulate a short write on the first fade command, run the
        sequence, and verify it returns failure and closes the serial port.
        """
        serial = FakeSerialModule(short_write_at=3)

        with redirect_stderr(StringIO()):
            self.assertFalse(fade.run_fade_sequence(serial, "COM6", 115200))

        self.assertEqual(len(serial.ports[0].writes), 3)
        self.assertTrue(serial.ports[0].closed)

    def test_main_routes_requested_port_to_the_fade_sequence(self) -> None:
        """Goal: expose the requested port through the `fade` CLI entry point.

        Steps: provide `--port COM6`, substitute a serial test double, and
        verify main forwards COM6 and the default baud rate to the sequence.
        """
        serial = FakeSerialModule()
        with (
            patch("sys.argv", ["fade", "--port", "COM6"]),
            patch.dict(sys.modules, {"serial": serial}),
            patch.object(fade, "run_fade_sequence", return_value=True) as run,
        ):
            self.assertEqual(fade.main(), 0)

        run.assert_called_once_with(serial, "COM6", 115200)
