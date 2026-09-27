"""Host tests for Rainbow3 protocol programming and smoke-test routing."""

from __future__ import annotations

import argparse
from contextlib import redirect_stderr
from io import StringIO
from unittest import TestCase
from unittest.mock import Mock, patch

from scripts import rainbow3
from scripts.modules import protocol
from scripts.smoke_test import SmokeTest, parse_args


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
    """Provide fake ports and record attempted serial opens."""

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


class Rainbow3WireTest(TestCase):
    """Verify the Rainbow3 command sequence without serial hardware."""

    def test_programs_group_zero_forever_with_default_marks(self) -> None:
        """Check frame order, payloads, checksums, and the Start sentinel."""
        serial = FakeSerialModule()

        self.assertTrue(rainbow3.run_rainbow3_sequence(serial, "COM8", 115200))

        writes = serial.ports[0].writes
        self.assertEqual(len(writes), 11)
        decoded = [unpack_frame(frame) for frame in writes]
        self.assertEqual(
            [tag for tag, _ in decoded],
            [
                protocol.TAG_RESET_INSTRUCTIONS,
                protocol.TAG_SET_MULTIPLE_COLOR_GROUPED,
                *([protocol.TAG_SET_SINGLE_COLOR_GROUPED] * 7),
                protocol.TAG_SET_MULTIPLE_COLOR_GROUPED,
                protocol.TAG_START,
            ],
        )
        self.assertEqual(decoded[0][1], b"")
        self.assertEqual(
            decoded[1][1],
            b"\x00\x00\x00\x00\x00\x00\x07\x00\x00\x00\x00",
        )

        for index, (color, (_, payload)) in enumerate(
            zip(rainbow3.RAINBOW_COLORS, decoded[2:9], strict=True)
        ):
            mark, red, green, blue, pixel = (
                int.from_bytes(payload[:2], "little"),
                *payload[2:6],
            )
            self.assertEqual(mark, index * 200)
            self.assertEqual((red, green, blue), color)
            self.assertEqual(pixel, index)
            self.assertEqual(payload[6:], b"\x00\x00\x00\x00")

        self.assertEqual(
            decoded[9][1],
            b"\x78\x05\x00\x00\x00\x00\x07\x00\x00\x00\x00",
        )
        self.assertEqual(
            decoded[9][1][2:],
            decoded[1][1][2:],
        )
        self.assertEqual(decoded[10], (protocol.TAG_START, b"\xFF\xFF"))
        self.assertEqual(writes[-1], b"\xAA\x0C\x01\x02\x00\xFF\xFF\x0F")
        self.assertNotIn(protocol.TAG_RUN, [tag for tag, _ in decoded])

    def test_override_changes_all_instruction_marks(self) -> None:
        """Use the requested delay for every palette mark."""
        serial = FakeSerialModule()

        self.assertTrue(
            rainbow3.run_rainbow3_sequence(
                serial, "COM8", 115200, step_delay_ms=17
            )
        )

        marks = [
            int.from_bytes(payload[:2], "little")
            for tag, payload in map(unpack_frame, serial.ports[0].writes)
            if tag == protocol.TAG_SET_SINGLE_COLOR_GROUPED
        ]
        self.assertEqual(marks, [0, 17, 34, 51, 68, 85, 102])
        final_clear = unpack_frame(serial.ports[0].writes[-2])[1]
        self.assertEqual(int.from_bytes(final_clear[:2], "little"), 119)

    def test_delay_bounds_and_invalid_values_before_open(self) -> None:
        """Accept mark-safe endpoints and reject invalid values before I/O."""
        for delay, final_mark in ((1, 7), (9362, 65534)):
            serial = FakeSerialModule()
            self.assertTrue(
                rainbow3.run_rainbow3_sequence(
                    serial, "COM8", 115200, step_delay_ms=delay
                )
            )
            final_payload = unpack_frame(serial.ports[0].writes[-2])[1]
            self.assertEqual(int.from_bytes(final_payload[:2], "little"), final_mark)

        for delay in (0, -1, 9363, 1.5, True):
            serial = FakeSerialModule()
            with redirect_stderr(StringIO()):
                self.assertFalse(
                    rainbow3.run_rainbow3_sequence(
                        serial, "COM8", 115200, step_delay_ms=delay
                    )
                )
            self.assertEqual(serial.ports, [])

    def test_program_short_write_fails(self) -> None:
        """Treat any incomplete program frame as a sequence failure."""
        serial = FakeSerialModule(short_write_at=3)

        with redirect_stderr(StringIO()):
            self.assertFalse(
                rainbow3.run_rainbow3_sequence(serial, "COM8", 115200)
            )

    def test_cleanup_checks_full_reset_write(self) -> None:
        """Succeed only when the complete reset frame reaches serial."""
        serial = FakeSerialModule()
        self.assertTrue(rainbow3.reset_rainbow3_program(serial, "COM8", 115200))
        self.assertEqual(serial.ports[0].writes, [b"\xAA\x02\x01\x00\x00\x03"])

        short_serial = FakeSerialModule(short_write_at=1)
        with redirect_stderr(StringIO()):
            self.assertFalse(
                rainbow3.reset_rainbow3_program(short_serial, "COM8", 115200)
            )


class SmokeRainbow3Test(TestCase):
    """Verify Rainbow3 smoke orchestration without opening a device."""

    @staticmethod
    def smoke_test(delay: int = 200) -> SmokeTest:
        """Create a smoke test instance with only its phase dependencies."""
        smoke = SmokeTest(
            argparse.Namespace(
                args=[],
                baudrate=115200,
                list_ports=False,
                rainbow_step_delay=delay,
                dump_logs=False,
                no_default_requirements=False,
            )
        )
        smoke.serial = object()
        smoke.role_ports = {"command": "COM8"}
        return smoke

    def test_cli_defaults_to_200_and_rejects_invalid_delay(self) -> None:
        """Validate the default and reject invalid CLI values at parse time."""
        with patch("sys.argv", ["smoke-test"]):
            self.assertEqual(parse_args().rainbow_step_delay, 200)

        with patch("sys.argv", ["smoke-test", "--rainbow-step-delay", "9363"]):
            with self.assertRaises(SystemExit):
                parse_args()

    @patch("scripts.smoke_test.threading.Thread")
    @patch("scripts.smoke_test.threading.Event")
    @patch("scripts.smoke_test.rainbow3.reset_rainbow3_program")
    @patch("scripts.smoke_test.rainbow3.run_rainbow3_sequence")
    def test_phase_forwards_delay_waits_and_cleans_up(
        self,
        run_sequence: Mock,
        reset_program: Mock,
        event_factory: Mock,
        thread_factory: Mock,
    ) -> None:
        """Forward the override, wait for one pass, and send cleanup."""
        run_sequence.return_value = True
        reset_program.return_value = True
        event = Mock()
        event_factory.return_value = event
        smoke = self.smoke_test(250)

        self.assertTrue(smoke._run_rainbow3_sequence())

        run_sequence.assert_called_once_with(
            smoke.serial, "COM8", 115200, step_delay_ms=250
        )
        event.wait.assert_called_once_with(2.25)
        reset_program.assert_called_once_with(smoke.serial, "COM8", 115200)
        thread_factory.return_value.start.assert_called_once()
        thread_factory.return_value.join.assert_called_once_with(timeout=1.0)

    @patch("scripts.smoke_test.threading.Thread")
    @patch("scripts.smoke_test.threading.Event")
    @patch("scripts.smoke_test.rainbow3.reset_rainbow3_program")
    @patch("scripts.smoke_test.rainbow3.run_rainbow3_sequence")
    def test_phase_wait_is_bounded_through_maximum_delay_clear(
        self,
        run_sequence: Mock,
        reset_program: Mock,
        event_factory: Mock,
        _thread_factory: Mock,
    ) -> None:
        """Wait through the final clear at the largest representable delay.

        Goal: Ensure maximum marks fit and the smoke pass includes the final
        clear, with a maximum observation wait of 66.034 seconds.
        Steps: Program a 9362 ms fake sequence, run the smoke phase, and check
        its wait duration and cleanup attempt.
        """
        run_sequence.return_value = True
        reset_program.return_value = True
        event = Mock()
        event_factory.return_value = event
        smoke = self.smoke_test(9362)

        self.assertTrue(smoke._run_rainbow3_sequence())

        event.wait.assert_called_once_with(66.034)
        reset_program.assert_called_once_with(smoke.serial, "COM8", 115200)

    @patch("scripts.smoke_test.threading.Thread")
    @patch("scripts.smoke_test.threading.Event")
    @patch("scripts.smoke_test.rainbow3.reset_rainbow3_program")
    @patch("scripts.smoke_test.rainbow3.run_rainbow3_sequence")
    def test_failed_program_skips_wait_but_attempts_cleanup(
        self,
        run_sequence: Mock,
        reset_program: Mock,
        event_factory: Mock,
        _thread_factory: Mock,
    ) -> None:
        """Attempt reset after partial programming without waiting."""
        run_sequence.return_value = False
        reset_program.return_value = True
        event = Mock()
        event_factory.return_value = event
        smoke = self.smoke_test()

        self.assertFalse(smoke._run_rainbow3_sequence())

        event.wait.assert_not_called()
        reset_program.assert_called_once_with(smoke.serial, "COM8", 115200)

    @patch("scripts.smoke_test.threading.Thread")
    @patch("scripts.smoke_test.threading.Event")
    @patch("scripts.smoke_test.rainbow3.reset_rainbow3_program")
    @patch("scripts.smoke_test.rainbow3.run_rainbow3_sequence")
    def test_cleanup_failure_fails_phase(
        self,
        run_sequence: Mock,
        reset_program: Mock,
        event_factory: Mock,
        _thread_factory: Mock,
    ) -> None:
        """Fail the rainbow phase when the cleanup frame is not written."""
        run_sequence.return_value = True
        reset_program.return_value = False
        event = Mock()
        event_factory.return_value = event
        smoke = self.smoke_test()

        self.assertFalse(smoke._run_rainbow3_sequence())
        reset_program.assert_called_once()

    @patch.object(SmokeTest, "_run_rainbow3_sequence", return_value=True)
    @patch.object(SmokeTest, "_run_loopback", return_value=True)
    @patch.object(SmokeTest, "_report_success")
    @patch.object(SmokeTest, "_validate_roles", return_value=True)
    @patch.object(SmokeTest, "_validate_markers", return_value=True)
    @patch.object(SmokeTest, "_capture_console")
    @patch.object(SmokeTest, "_resolve_roles_by_interface")
    @patch.object(SmokeTest, "_discover_ports")
    @patch.object(SmokeTest, "_import_serial", return_value=True)
    def test_smoke_default_routes_to_rainbow3_and_skip_bypasses_it(
        self,
        _import_serial: Mock,
        _discover_ports: Mock,
        _resolve_roles: Mock,
        _capture_console: Mock,
        _validate_markers: Mock,
        _validate_roles: Mock,
        _report_success: Mock,
        loopback: Mock,
        rainbow_phase: Mock,
    ) -> None:
        """Run Rainbow3 by default and skip its entire phase when requested."""
        args = argparse.Namespace(
            list_ports=False,
            skip_rainbow=False,
            dump_logs=False,
        )
        smoke = SmokeTest(args)
        self.assertEqual(smoke.run(), 0)
        rainbow_phase.assert_called_once()

        args.skip_rainbow = True
        smoke = SmokeTest(args)
        self.assertEqual(smoke.run(), 0)
        rainbow_phase.assert_called_once()
        loopback.assert_called()
