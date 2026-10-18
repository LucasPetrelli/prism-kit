"""Host tests for Rainbow4 protocol programming and stop/reset behavior."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import sys
from unittest import TestCase
from unittest.mock import patch

from scripts.device import rainbow4
from scripts.device.modules import protocol
from scripts.device.rainbow2 import RAINBOW_COLORS


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


class Rainbow4WireTest(TestCase):
    """Verify the complete Rainbow4 program without serial hardware."""

    def test_programs_all_thirty_frames_with_grouped_fades(self) -> None:
        """Check frame order, payloads, checksums, and root Start(0)."""
        serial = FakeSerialModule()
        output = StringIO()

        with redirect_stdout(output):
            self.assertTrue(rainbow4.run_rainbow4_sequence(serial, "COM8", 115200))

        port = serial.ports[0]
        self.assertTrue(port.closed)
        self.assertEqual(len(port.writes), 30)
        decoded = [unpack_frame(frame) for frame in port.writes]

        expected_tags = [protocol.TAG_RESET_INSTRUCTIONS]
        for _ in RAINBOW_COLORS:
            expected_tags.extend(
                [protocol.TAG_SET_SINGLE_COLOR_GROUPED, protocol.TAG_RUN_GROUP]
            )
        expected_tags.extend([protocol.TAG_FADE] * 14)
        expected_tags.append(protocol.TAG_START)
        self.assertEqual([tag for tag, _ in decoded], expected_tags)
        self.assertEqual(decoded[0], (protocol.TAG_RESET_INSTRUCTIONS, b""))

        expected_marks = [0, 286, 571, 857, 1143, 1429, 1714]
        for index, (color, mark) in enumerate(
            zip(RAINBOW_COLORS, expected_marks, strict=True)
        ):
            color_payload = decoded[1 + (2 * index)][1]
            self.assertEqual(len(color_payload), 10)
            self.assertEqual(int.from_bytes(color_payload[0:2], "little"), mark)
            self.assertEqual(tuple(color_payload[2:5]), color)
            self.assertEqual(color_payload[5], index)
            self.assertEqual(
                int.from_bytes(color_payload[6:10], "little"),
                0,
            )

            run_group_payload = decoded[2 + (2 * index)][1]
            self.assertEqual(len(run_group_payload), 12)
            self.assertEqual(
                int.from_bytes(run_group_payload[0:4], "little"),
                0,
            )
            self.assertEqual(
                int.from_bytes(run_group_payload[4:6], "little"), mark
            )
            self.assertEqual(
                int.from_bytes(run_group_payload[6:10], "little"), index + 1
            )
            self.assertEqual(
                int.from_bytes(run_group_payload[10:12], "little"), 0xFFFF
            )

            fade_down = decoded[15 + (2 * index)][1]
            fade_up = decoded[16 + (2 * index)][1]
            for payload in (fade_down, fade_up):
                self.assertEqual(len(payload), 11)
                self.assertEqual(payload[6], index)
                self.assertEqual(
                    int.from_bytes(payload[7:11], "little"), index + 1
                )
                self.assertEqual(int.from_bytes(payload[4:6], "little"), 2000)
            self.assertEqual(int.from_bytes(fade_down[0:2], "little"), 0)
            self.assertEqual(int.from_bytes(fade_down[2:4], "little"), 25)
            self.assertEqual(int.from_bytes(fade_up[0:2], "little"), 2000)
            self.assertEqual(int.from_bytes(fade_up[2:4], "little"), 400)

        self.assertEqual(decoded[-1], (protocol.TAG_START, b"\x00\x00"))
        self.assertIn("cannot confirm controller acceptance", output.getvalue())

    def test_program_short_write_fails(self) -> None:
        """Treat an incomplete program frame as a sequence failure."""
        serial = FakeSerialModule(short_write_at=17)

        with redirect_stderr(StringIO()):
            self.assertFalse(rainbow4.run_rainbow4_sequence(serial, "COM8", 115200))

        self.assertEqual(len(serial.ports[0].writes), 17)
        self.assertTrue(serial.ports[0].closed)

    def test_stop_helper_sends_only_reset_and_preserves_staged_colors(self) -> None:
        """Reset the program without claiming to restore the LED colors."""
        serial = FakeSerialModule()
        output = StringIO()

        with redirect_stdout(output):
            self.assertTrue(rainbow4.reset_rainbow4_program(serial, "COM8", 115200))

        self.assertEqual(serial.ports[0].writes, [b"\xAA\x02\x01\x00\x00\x03"])
        self.assertIn("current LED colors are unchanged", output.getvalue())
        self.assertIn("cannot confirm controller acceptance", output.getvalue())

    def test_stop_helper_short_write_fails(self) -> None:
        """Report an incomplete stop/reset frame as a failure."""
        serial = FakeSerialModule(short_write_at=1)

        with redirect_stderr(StringIO()):
            self.assertFalse(rainbow4.reset_rainbow4_program(serial, "COM8", 115200))

    def test_stop_option_routes_to_reset_helper(self) -> None:
        """Select stop mode from the command-line entry point."""
        serial = FakeSerialModule()
        with (
            patch("sys.argv", ["rainbow4", "--port", "COM8", "--stop"]),
            patch.dict(sys.modules, {"serial": serial}),
            patch.object(rainbow4, "reset_rainbow4_program", return_value=True) as reset,
        ):
            self.assertEqual(rainbow4.main(), 0)

        reset.assert_called_once_with(serial, "COM8", 115200)
