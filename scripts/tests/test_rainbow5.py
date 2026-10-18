"""Host tests for Rainbow5 frame generation, mapping, and stop behavior."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import sys
from unittest import TestCase
from unittest.mock import patch

from scripts.device import rainbow5


SYNC_BYTE = 0xAA
TAG_RESET_INSTRUCTIONS = 0x0102
TAG_START = 0x010C
TAG_GRADIENT = 0x010E
EXPECTED_HSV_COLORS = {
    0: (191, 0, 0),
    30: (191, 96, 0),
    60: (191, 191, 0),
    90: (96, 191, 0),
    120: (0, 191, 0),
    150: (0, 191, 96),
    180: (0, 191, 191),
    210: (0, 96, 191),
    240: (0, 0, 191),
    270: (96, 0, 191),
    300: (191, 0, 191),
    330: (191, 0, 96),
}


class FakePort:
    """Capture protocol writes like an open serial port."""

    def __init__(self) -> None:
        self.writes: list[bytes] = []
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
        """Capture a complete frame write."""
        self.writes.append(data)
        return len(data)


class FakeSerialModule:
    """Provide fake ports without opening a serial device."""

    def __init__(self) -> None:
        self.ports: list[FakePort] = []

    def Serial(self, *_: object, **__: object) -> FakePort:
        """Return a fresh capture port."""
        port = FakePort()
        self.ports.append(port)
        return port


def decode_frame(frame: bytes) -> tuple[int, bytes]:
    """Check a complete frame's bytes and XOR checksum, then decode it."""
    if len(frame) < 6:
        raise AssertionError("protocol frame is shorter than its header")
    if frame[0] != SYNC_BYTE:
        raise AssertionError("protocol frame has an invalid sync byte")

    tag = int.from_bytes(frame[1:3], "little")
    payload_size = int.from_bytes(frame[3:5], "little")
    if len(frame) != payload_size + 6:
        raise AssertionError("protocol frame length does not match its header")

    payload = frame[5:-1]
    checksum = 0
    for value in frame[1:-1]:
        checksum ^= value
    if frame[-1] != checksum:
        raise AssertionError("protocol frame checksum is incorrect")

    return tag, payload


def expected_frame(tag: int, payload: bytes) -> bytes:
    """Create expected raw bytes with an independent checksum calculation."""
    header = tag.to_bytes(2, "little") + len(payload).to_bytes(2, "little")
    checksum = 0
    for value in header + payload:
        checksum ^= value
    return bytes([SYNC_BYTE]) + header + payload + bytes([checksum])


class Rainbow5Test(TestCase):
    """Validate the programmed Rainbow5 frame sequence."""

    def test_frames_encode_ordered_mapping_schedule_and_repeat(self) -> None:
        """Goal: Verify an ordered mapping produces the complete Rainbow5 program.

        Steps: Capture writes for a nonsequential mapping, then compare all 28
        frames, grouped waypoint payloads, checksums, and the repeating Start.
        """
        ring_indices = (4, 1, 6, 0, 2, 5)
        center_index = 3
        serial_module = FakeSerialModule()

        with redirect_stdout(StringIO()):
            self.assertTrue(
                rainbow5.run_rainbow5_sequence(
                    serial_module,
                    "COM8",
                    115200,
                    rainbow5.Rainbow5Mapping(ring_indices, center_index),
                )
            )

        self.assertEqual(len(serial_module.ports), 1)
        port = serial_module.ports[0]
        self.assertTrue(port.closed)
        self.assertEqual(len(port.writes), 28)

        decoded = [decode_frame(frame) for frame in port.writes]
        tags = [tag for tag, _ in decoded]
        self.assertEqual(
            tags,
            [TAG_RESET_INSTRUCTIONS]
            + [TAG_GRADIENT] * 26
            + [TAG_START],
        )
        self.assertEqual(
            port.writes[0], expected_frame(TAG_RESET_INSTRUCTIONS, b"")
        )

        expected_gradients: list[tuple[int, tuple[int, int, int], int, int]] = []
        for waypoint, mark in enumerate((0, 1500, 3000, 4500)):
            for logical_position, led_index in enumerate(ring_indices):
                hue = (60 * logical_position - 90 * waypoint) % 360
                expected_gradients.append(
                    (mark, EXPECTED_HSV_COLORS[hue], 1500, led_index)
                )
            if mark == 0:
                expected_gradients.append((0, (255, 240, 214), 3000, center_index))
            elif mark == 3000:
                expected_gradients.append((3000, (200, 184, 255), 3000, center_index))

        self.assertEqual(len(expected_gradients), 26)
        for frame, (mark, color, duration, led_index) in zip(
            port.writes[1:-1], expected_gradients, strict=True
        ):
            payload = (
                mark.to_bytes(2, "little")
                + bytes(color)
                + duration.to_bytes(2, "little")
                + bytes([led_index])
                + (0).to_bytes(4, "little")
            )
            self.assertEqual(frame, expected_frame(TAG_GRADIENT, payload))
            self.assertEqual(decode_frame(frame), (TAG_GRADIENT, payload))

        start_payload = (0xFFFF).to_bytes(2, "little")
        self.assertEqual(port.writes[-1], expected_frame(TAG_START, start_payload))
        self.assertEqual(decoded[-1], (TAG_START, start_payload))

    def test_cli_uses_default_mapping_when_only_port_is_supplied(self) -> None:
        """Goal: Program Rainbow5 with the documented default LED mapping.

        Steps: Run the CLI with only ``--port`` and fake serial; verify the
        first mark maps ring positions to 0..5 and the center to index 6.
        """
        serial_module = FakeSerialModule()

        with (
            patch.dict(sys.modules, {"serial": serial_module}),
            patch.object(sys, "argv", ["rainbow5", "--port", "COM8"]),
            redirect_stdout(StringIO()),
        ):
            self.assertEqual(rainbow5.main(), 0)

        self.assertEqual(len(serial_module.ports), 1)
        port = serial_module.ports[0]
        self.assertEqual(len(port.writes), 28)
        first_mark = [decode_frame(frame) for frame in port.writes[1:8]]
        self.assertEqual([tag for tag, _ in first_mark], [TAG_GRADIENT] * 7)
        self.assertEqual(
            [payload[7] for _, payload in first_mark],
            [0, 1, 2, 3, 4, 5, 6],
        )
        center_payload = first_mark[-1][1]
        self.assertEqual(center_payload[:2], b"\x00\x00")
        self.assertEqual(tuple(center_payload[2:5]), (255, 240, 214))
        self.assertEqual(int.from_bytes(center_payload[5:7], "little"), 3000)
        self.assertEqual(int.from_bytes(center_payload[8:12], "little"), 0)

    def test_valid_permutation_accepts_any_physical_mapping(self) -> None:
        """Goal: Accept a valid mapping whose ring order differs from LED order.

        Steps: Validate a nonsequential ring permutation and confirm the first
        Gradient targets the LED mapped to logical ring position zero.
        """
        ring_indices = (6, 2, 4, 0, 5, 1)
        center_index = 3

        self.assertEqual(
            rainbow5.validate_mapping(ring_indices, center_index),
            (ring_indices, center_index),
        )
        frames = rainbow5.build_rainbow5_frames(ring_indices, center_index)
        first_gradient = decode_frame(frames[1])
        self.assertEqual(first_gradient[0], TAG_GRADIENT)
        self.assertEqual(first_gradient[1][7], ring_indices[0])

    def test_invalid_mappings_fail_before_serial_open(self) -> None:
        """Goal: Reject malformed mappings before opening the command port.

        Steps: Try duplicate, short, overlapping, out-of-range, and non-integer
        mappings with fake serial; verify each fails without creating a port.
        """
        invalid_mappings = (
            ((0, 0, 2, 3, 4, 5), 1),
            ((0, 1, 2, 3, 4), 6),
            ((0, 1, 2, 3, 4, 5), 5),
            ((0, 1, 2, 3, 4, 7), 6),
            ((0, 1, 2, 3, 4, 5), 7),
            ((0, 1, 2, 3, 4, "5"), 6),  # type: ignore[arg-type]
        )
        for ring_indices, center_index in invalid_mappings:
            with self.subTest(ring_indices=ring_indices, center_index=center_index):
                serial_module = FakeSerialModule()
                with redirect_stderr(StringIO()):
                    self.assertFalse(
                        rainbow5.run_rainbow5_sequence(
                            serial_module,
                            "COM8",
                            115200,
                            rainbow5.Rainbow5Mapping(
                                ring_indices, center_index
                            ),
                        )
                    )
                self.assertEqual(serial_module.ports, [])

    def test_stop_option_sends_reset_only(self) -> None:
        """Goal: Make ``--stop`` cancel queued work without changing LED colors.

        Steps: Run the CLI with fake serial and ``--stop``; verify it writes one
        exact Reset frame and reports unchanged colors and one-way status.
        """
        serial_module = FakeSerialModule()
        stdout = StringIO()

        with (
            patch.dict(sys.modules, {"serial": serial_module}),
            patch.object(sys, "argv", ["rainbow5", "--port", "COM8", "--stop"]),
            redirect_stdout(stdout),
        ):
            self.assertEqual(rainbow5.main(), 0)

        self.assertEqual(len(serial_module.ports), 1)
        self.assertTrue(serial_module.ports[0].closed)
        self.assertEqual(
            serial_module.ports[0].writes,
            [expected_frame(TAG_RESET_INSTRUCTIONS, b"")],
        )
        self.assertIn("colors are unchanged", stdout.getvalue())
        self.assertIn("cannot confirm controller acceptance", stdout.getvalue())
