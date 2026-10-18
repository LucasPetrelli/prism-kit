"""Host tests for grouped Gradient frame programming."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import sys
from unittest import TestCase
from unittest.mock import patch

from scripts import gradient
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
        self.calls: list[tuple[tuple[object, ...], dict[str, object]]] = []
        self.short_write_at = short_write_at

    def Serial(self, *args: object, **kwargs: object) -> FakePort:
        """Return a fresh capture port and record its serial settings."""
        self.calls.append((args, kwargs))
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


class GradientWireTest(TestCase):
    """Verify the Gradient command and CLI without serial hardware."""

    def test_builder_encodes_exact_twelve_byte_little_endian_payload(self) -> None:
        """Goal: Check the exact Gradient wire fields and checksum.

        Steps: Build a frame with distinct multi-byte values, then compare its
        tag, declared length, 12-byte payload, and decoded checksum.
        """
        frame = protocol.build_gradient_grouped_frame(
            0x1234,
            0xA5,
            0x5A,
            0xC3,
            0x5678,
            0x09,
            0x89ABCDEF,
        )

        expected_payload = bytes(
            [
                0x34,
                0x12,
                0xA5,
                0x5A,
                0xC3,
                0x78,
                0x56,
                0x09,
                0xEF,
                0xCD,
                0xAB,
                0x89,
            ]
        )
        self.assertEqual(len(expected_payload), 12)
        self.assertEqual(
            frame[:5],
            bytes([protocol.SYNC_BYTE, 0x0E, 0x01, 0x0C, 0x00]),
        )
        self.assertEqual(frame[5:-1], expected_payload)
        self.assertEqual(unpack_frame(frame), (0x010E, expected_payload))

    def test_programs_reset_gradient_and_one_pass_start_in_order(self) -> None:
        """Goal: Verify one root-group Gradient program over fake serial.

        Steps: Program LED 3 through a fake port using non-default baud rate,
        mark, target, and duration; check Reset/Gradient/Start order, payload,
        checksums, serial settings, port closure, and one-way status text.
        """
        serial = FakeSerialModule()
        output = StringIO()

        with redirect_stdout(output):
            self.assertTrue(
                gradient.run_gradient_sequence(
                    serial,
                    "COM6",
                    230400,
                    3,
                    (0x12, 0x34, 0x56),
                    1500,
                    0x1234,
                )
            )

        port = serial.ports[0]
        self.assertTrue(port.closed)
        decoded = [unpack_frame(frame) for frame in port.writes]
        self.assertEqual(
            [tag for tag, _ in decoded],
            [
                protocol.TAG_RESET_INSTRUCTIONS,
                protocol.TAG_GRADIENT,
                protocol.TAG_START,
            ],
        )
        self.assertEqual(decoded[0], (protocol.TAG_RESET_INSTRUCTIONS, b""))
        self.assertEqual(
            decoded[1],
            (
                protocol.TAG_GRADIENT,
                bytes(
                    [
                        0x34,
                        0x12,
                        0x12,
                        0x34,
                        0x56,
                        0xDC,
                        0x05,
                        3,
                        0,
                        0,
                        0,
                        0,
                    ]
                ),
            ),
        )
        self.assertEqual(decoded[-1], (protocol.TAG_START, b"\x00\x00"))
        self.assertEqual(
            serial.calls,
            [
                (
                    ("COM6",),
                    {"baudrate": 230400, "timeout": 0.1, "write_timeout": 1.0},
                )
            ],
        )
        self.assertIn("cannot confirm controller acceptance", output.getvalue())

    def test_short_gradient_write_fails_and_closes_the_port(self) -> None:
        """Goal: Reject a partial Gradient frame write.

        Steps: Make the fake port short-write the second frame, then verify the
        sequence fails, closes the port, and does not send Start.
        """
        serial = FakeSerialModule(short_write_at=2)

        with redirect_stderr(StringIO()):
            self.assertFalse(
                gradient.run_gradient_sequence(
                    serial, "COM6", 115200, 3, (0, 0, 255), 1500
                )
            )

        self.assertEqual(len(serial.ports[0].writes), 2)
        self.assertTrue(serial.ports[0].closed)

    def test_main_routes_cli_values_to_gradient_sequence(self) -> None:
        """Goal: Route parsed command-line values to the host sequence.

        Steps: Supply an explicit port, baud rate, index, target, duration,
        and mark, then verify main forwards their parsed values and returns
        the sequence result.
        """
        serial = FakeSerialModule()
        argv = [
            "gradient",
            "--port",
            "COM6",
            "--index",
            "3",
            "--target",
            "#123456",
            "--duration-ms",
            "1500",
            "--mark",
            "25",
            "--baudrate",
            "230400",
        ]
        with (
            patch("sys.argv", argv),
            patch.dict(sys.modules, {"serial": serial}),
            patch.object(gradient, "run_gradient_sequence", return_value=True) as run,
        ):
            self.assertEqual(gradient.main(), 0)

        run.assert_called_once_with(
            serial,
            "COM6",
            230400,
            3,
            (0x12, 0x34, 0x56),
            1500,
            25,
        )

