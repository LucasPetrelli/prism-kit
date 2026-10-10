"""Host tests for the one-shot set-color command."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
import sys
from types import ModuleType
from unittest import TestCase
from unittest.mock import patch

from scripts import set_color
from scripts.modules import protocol
from scripts.modules.serial_utils import PortInfo


class FakePort:
    """Capture protocol writes like an open serial port."""

    def __init__(self, short_write_at: int | None = None) -> None:
        self.writes: list[bytes] = []
        self.short_write_at = short_write_at
        self.closed = False
        self.input_buffer_reset = False
        self.output_buffer_reset = False

    def __enter__(self) -> FakePort:
        """Return this port from a ``with`` block."""
        return self

    def __exit__(self, *_: object) -> None:
        """Record that the serial context has closed."""
        self.closed = True

    def reset_input_buffer(self) -> None:
        """Record the input-buffer reset requested by the command."""
        self.input_buffer_reset = True

    def reset_output_buffer(self) -> None:
        """Record the output-buffer reset requested by the command."""
        self.output_buffer_reset = True

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
        """Record the requested port and return a capture port."""
        self.calls.append((args, kwargs))
        port = FakePort(self.short_write_at)
        self.ports.append(port)
        return port


class FakeListPorts:
    """Provide a fixed serial-port snapshot for discovery tests."""

    def __init__(self, ports: list[PortInfo]) -> None:
        self.ports = ports

    def comports(self) -> list[PortInfo]:
        """Return the configured port metadata."""
        return self.ports


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


def prism_kit_ports() -> list[PortInfo]:
    """Build debug and command CDC port metadata for one test board."""
    return [
        PortInfo(
            device="COM7",
            description="Prism Kit debug",
            product="Prism Kit",
            manufacturer="Example",
            serial_number="PK-TEST",
            hwid=r"USB\VID_1234&PID_5678&MI_00\PK-TEST",
        ),
        PortInfo(
            device="COM8",
            description="Prism Kit command",
            product="Prism Kit",
            manufacturer="Example",
            serial_number="PK-TEST",
            hwid=r"USB\VID_1234&PID_5678&MI_01\PK-TEST",
        ),
    ]


class SetColorWireTest(TestCase):
    """Verify set-color frames without serial hardware."""

    def test_sets_selected_led_and_zeros_every_other_led(self) -> None:
        """Goal: apply one RGB value while blacking out every other pixel.

        Steps: capture the frames for LED 3, decode their checksums and
        payloads, then verify Reset, seven explicit pixel values at mark 0,
        and one Run frame in that order.
        """
        serial = FakeSerialModule()
        output = StringIO()
        color = (0x12, 0xAB, 0x34)

        with redirect_stdout(output):
            self.assertTrue(set_color.run_set_color(serial, "COM8", 115200, 3, color))

        port = serial.ports[0]
        self.assertTrue(port.closed)
        self.assertTrue(port.input_buffer_reset)
        self.assertTrue(port.output_buffer_reset)
        self.assertEqual(serial.calls[0][0], ("COM8",))
        self.assertEqual(serial.calls[0][1]["baudrate"], 115200)
        self.assertEqual(len(port.writes), 9)

        decoded = [unpack_frame(frame) for frame in port.writes]
        self.assertEqual(
            [tag for tag, _ in decoded],
            [
                protocol.TAG_RESET_INSTRUCTIONS,
                *([protocol.TAG_SET_SINGLE_COLOR] * set_color.LED_COUNT),
                protocol.TAG_RUN,
            ],
        )
        self.assertEqual(decoded[0], (protocol.TAG_RESET_INSTRUCTIONS, b""))

        for led_index, (tag, payload) in enumerate(decoded[1:-1]):
            self.assertEqual(tag, protocol.TAG_SET_SINGLE_COLOR)
            self.assertEqual(int.from_bytes(payload[0:2], "little"), 0)
            expected_color = color if led_index == 3 else (0, 0, 0)
            self.assertEqual(payload[2:5], bytes(expected_color))
            self.assertEqual(payload[5], led_index)

        self.assertEqual(decoded[-1], (protocol.TAG_RUN, b""))
        self.assertIn("all other LEDs set to black", output.getvalue())
        self.assertIn("cannot confirm controller acceptance", output.getvalue())

    def test_rejects_out_of_range_index_before_opening_serial(self) -> None:
        """Goal: prevent an invalid pixel index from reaching the wire.

        Steps: request indices below and above the supported range, then
        verify each call fails without opening a serial port.
        """
        for index in (-1, 7):
            with self.subTest(index=index):
                serial = FakeSerialModule()
                with redirect_stderr(StringIO()):
                    self.assertFalse(
                        set_color.run_set_color(
                            serial, "COM8", 115200, index, (1, 2, 3)
                        )
                    )
                self.assertEqual(serial.ports, [])

    def test_short_write_fails_and_closes_the_port(self) -> None:
        """Goal: report failure when a protocol frame is only partly written.

        Steps: simulate a short write on the first pixel frame, run the
        command, and verify it stops sending and closes the port.
        """
        serial = FakeSerialModule(short_write_at=2)

        with redirect_stderr(StringIO()):
            self.assertFalse(
                set_color.run_set_color(serial, "COM8", 115200, 0, (1, 2, 3))
            )

        self.assertEqual(len(serial.ports[0].writes), 2)
        self.assertTrue(serial.ports[0].closed)


class SetColorCliTest(TestCase):
    """Verify set-color parsing and command-port selection."""

    def test_parses_requested_positionals_and_hex_color(self) -> None:
        """Goal: accept the documented two-positional invocation.

        Steps: parse ``set-color 6 #aB01fF`` without a port override, then
        verify the index, RGB bytes, and automatic-port selection default.
        """
        with patch("sys.argv", ["set-color", "6", "#aB01fF"]):
            args = set_color.parse_args()

        self.assertEqual(args.index, 6)
        self.assertEqual(args.color, (0xAB, 0x01, 0xFF))
        self.assertIsNone(args.port)

    def test_rejects_invalid_index_and_color_arguments(self) -> None:
        """Goal: reject values outside the CLI's index and RGB syntax.

        Steps: pass an out-of-range index and malformed hex colors to the
        argparse converters, then verify each is reported as a usage error.
        """
        for invalid_index in ("-1", "7", "x"):
            with self.subTest(index=invalid_index):
                with (
                    redirect_stderr(StringIO()),
                    patch("sys.argv", ["set-color", invalid_index, "FF0000"]),
                ):
                    with self.assertRaises(SystemExit):
                        set_color.parse_args()

        for invalid_color in ("12345", "1234567", "GG0000", "12 0000"):
            with self.subTest(color=invalid_color):
                with (
                    redirect_stderr(StringIO()),
                    patch("sys.argv", ["set-color", "0", invalid_color]),
                ):
                    with self.assertRaises(SystemExit):
                        set_color.parse_args()

    def test_auto_detects_the_command_interface(self) -> None:
        """Goal: select the command CDC interface for a single Prism Kit.

        Steps: provide debug and command port metadata sharing a board serial
        number, then verify auto-detection selects the higher USB interface.
        """
        ports = FakeListPorts(prism_kit_ports())

        self.assertEqual(set_color.resolve_command_port(ports), "COM8")

    def test_auto_detection_requires_a_recognizable_command_port(self) -> None:
        """Goal: avoid guessing when no command interface can be identified.

        Steps: expose only the board's debug interface, then verify discovery
        asks the caller to specify ``--port`` instead of choosing a device.
        """
        ports = FakeListPorts(prism_kit_ports()[:1])

        with self.assertRaisesRegex(RuntimeError, "--port"):
            set_color.resolve_command_port(ports)

    def test_main_forwards_explicit_port_and_color(self) -> None:
        """Goal: honor an explicit command port from the installed CLI.

        Steps: invoke ``set-color 2 01FEA0 --port COM9`` with a serial fake,
        then verify main succeeds and opens the requested device.
        """
        serial = FakeSerialModule()
        output = StringIO()

        with (
            patch("sys.argv", ["set-color", "2", "01FEA0", "--port", "COM9"]),
            patch.dict(sys.modules, {"serial": serial}),
            redirect_stdout(output),
        ):
            self.assertEqual(set_color.main(), 0)

        self.assertEqual(serial.calls[0][0], ("COM9",))
        self.assertIn("LED 2 set to #01FEA0 on COM9", output.getvalue())

    def test_main_auto_detects_command_port_for_documented_invocation(self) -> None:
        """Goal: run the two-positional command without a port override.

        Steps: provide one fake Prism Kit board through pyserial port metadata,
        invoke the documented positional form, and verify it opens COM8.
        """
        serial_fake = FakeSerialModule()
        serial_package = ModuleType("serial")
        serial_package.Serial = serial_fake.Serial
        serial_package.__path__ = []
        serial_tools = ModuleType("serial.tools")
        serial_tools.list_ports = FakeListPorts(prism_kit_ports())
        serial_package.tools = serial_tools
        output = StringIO()

        with (
            patch("sys.argv", ["set-color", "3", "00FF20"]),
            patch.dict(
                sys.modules,
                {"serial": serial_package, "serial.tools": serial_tools},
            ),
            redirect_stdout(output),
        ):
            self.assertEqual(set_color.main(), 0)

        self.assertEqual(serial_fake.calls[0][0], ("COM8",))
        self.assertIn("LED 3 set to #00FF20 on COM8", output.getvalue())
