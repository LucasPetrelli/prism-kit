"""Host tests for USB CDC console capture behavior."""

from __future__ import annotations

import time
from unittest import TestCase
from unittest.mock import patch

from scripts.modules.serial_utils import PortInfo, capture_console


class _DisconnectedPort:
    """Simulate a serial handle invalidated by a board reset."""

    def __init__(self) -> None:
        self.closed = False

    def readline(self) -> bytes:
        raise OSError("device disconnected")

    def close(self) -> None:
        self.closed = True


class _ReconnectedPort:
    """Return one startup line after the USB CDC device reconnects."""

    def __init__(self) -> None:
        self.lines = [b"Booting Zephyr OS build test\r\n"]
        self.closed = False

    def readline(self) -> bytes:
        if self.lines:
            return self.lines.pop(0)
        time.sleep(0.005)
        return b""

    def close(self) -> None:
        self.closed = True


class _SerialModule:
    """Return a failing handle followed by a reconnected handle."""

    def __init__(self) -> None:
        self.open_count = 0
        self.ports: list[_DisconnectedPort | _ReconnectedPort] = []

    def Serial(
        self, *_: object, **__: object
    ) -> _DisconnectedPort | _ReconnectedPort:
        self.open_count += 1
        port = _DisconnectedPort() if self.open_count == 1 else _ReconnectedPort()
        self.ports.append(port)
        return port


class SerialConsoleCaptureTest(TestCase):
    """Verify optional recovery from USB CDC disconnects during capture."""

    def test_reopens_same_port_after_reset_disconnect(self) -> None:
        """Reopen the CDC path after reset invalidates the serial handle.

        Goal: Preserve startup log capture when a board reset briefly disconnects
        its USB CDC device.
        Steps: Make the first serial handle fail on read, return a replacement
        handle with the boot marker, then verify capture retries and records it.
        """
        serial = _SerialModule()
        port = PortInfo(
            device="COM8",
            description="USB Serial Device",
            product=None,
            manufacturer=None,
            serial_number="board",
            hwid="USB VID:PID=2FE3:0004",
        )

        with patch("builtins.print"):
            captured, seen_required, _, _ = capture_console(
                serial,
                [port],
                capture_timeout=0.3,
                required_markers=("Booting Zephyr OS build",),
                quiet=True,
                stop_early=False,
                reconnect_disconnected_ports=True,
            )

        self.assertEqual(serial.open_count, 2)
        self.assertTrue(serial.ports[0].closed)
        self.assertIn("Booting Zephyr OS build test", captured["COM8"][0])
        self.assertEqual(seen_required, {"Booting Zephyr OS build"})
