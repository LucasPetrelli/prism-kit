#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Set one prism-kit LED to an RGB hex color and turn the others off.

Usage::

    uv run set-color 3 FF00AA
    uv run set-color 3 FF00AA --port COM8
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

_REPO_ROOT = str(Path(__file__).resolve().parents[2])
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from scripts.device.modules.protocol import (  # noqa: E402
    build_reset_instructions_frame,
    build_run_frame,
    build_set_single_color_frame,
)
from scripts.device.modules.serial_utils import (  # noqa: E402
    PortInfo,
    as_port_info,
    describe_port,
    group_ports_by_device,
    port_matches_tokens,
)
from scripts.device.smoke_test import (  # noqa: E402
    DEFAULT_PORT_MATCH_TOKENS,
    resolve_roles_by_interface,
)

LED_COUNT = 7
DEFAULT_BAUDRATE = 115200
BLACK_COLOR = (0, 0, 0)
_HEX_COLOR_PATTERN = re.compile(r"[0-9A-Fa-f]{6}")


def parse_led_index(value: str) -> int:
    """Parse an LED index in the supported zero-based range.

    Args:
        value: Text supplied for the LED index.

    Returns:
        The LED index in ``0..6``.

    Raises:
        argparse.ArgumentTypeError: If the value is not an index in ``0..6``.
    """
    try:
        index = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "index must be an integer from 0 to 6"
        ) from exc

    if not 0 <= index < LED_COUNT:
        raise argparse.ArgumentTypeError("index must be from 0 to 6")
    return index


def parse_rgb_color(value: str) -> tuple[int, int, int]:
    """Parse a six-digit RGB hex color, optionally prefixed with ``#``.

    Args:
        value: Six hexadecimal digits in ``RRGGBB`` order.

    Returns:
        Red, green, and blue channel values in ``0..255``.

    Raises:
        argparse.ArgumentTypeError: If the value is not exactly six hex digits.
    """
    digits = value[1:] if value.startswith("#") else value
    if _HEX_COLOR_PATTERN.fullmatch(digits) is None:
        raise argparse.ArgumentTypeError(
            "color must be six hex digits in RRGGBB format"
        )

    return (
        int(digits[0:2], 16),
        int(digits[2:4], 16),
        int(digits[4:6], 16),
    )


def resolve_command_port(list_ports_module: Any) -> str:
    """Auto-detect the unique Prism Kit command serial interface.

    Args:
        list_ports_module: Pyserial-compatible module providing ``comports``.

    Returns:
        The command-port device name.

    Raises:
        RuntimeError: If a unique Prism Kit command port cannot be identified.
    """
    visible_ports = [as_port_info(port) for port in list_ports_module.comports()]
    prism_ports = [
        port
        for port in visible_ports
        if port_matches_tokens(port, DEFAULT_PORT_MATCH_TOKENS)
    ]

    if not prism_ports:
        visible = "\n".join(f"- {describe_port(port)}" for port in visible_ports)
        if not visible:
            visible = "- none"
        raise RuntimeError(
            "No serial ports matched the Prism Kit device. Connect the board or "
            "specify --port.\nVisible ports:\n"
            f"{visible}"
        )

    command_devices: list[str] = []
    for device_ports in group_ports_by_device(prism_ports).values():
        roles = resolve_roles_by_interface(device_ports)
        command_device = roles["command"]
        if command_device is not None and command_device not in command_devices:
            command_devices.append(command_device)

    if len(command_devices) != 1:
        if command_devices:
            candidates = ", ".join(command_devices)
            raise RuntimeError(
                "Multiple Prism Kit command ports were detected "
                f"({candidates}); specify --port to choose one."
            )
        raise RuntimeError(
            "Could not identify the Prism Kit command interface. "
            "Specify it with --port (for example, --port COM8)."
        )

    return command_devices[0]


def _write_frame(port: Any, frame: bytes) -> None:
    """Write one complete protocol frame or raise on a short serial write.

    Args:
        port: Open serial port providing ``write``.
        frame: Encoded protocol frame.

    Raises:
        OSError: If the serial port accepts fewer than all frame bytes.
    """
    written = port.write(frame)
    if written != len(frame):
        raise OSError(f"short serial write ({written}/{len(frame)} bytes)")


def run_set_color(
    serial_module: Any,
    command_device: str,
    baudrate: int,
    index: int,
    color: tuple[int, int, int],
) -> bool:
    """Set one LED to *color* and explicitly turn all other LEDs off.

    The reset, seven pixel instructions, and Run frame are sent as one
    one-shot program. A successful write cannot confirm controller acceptance
    because the command protocol has no response frame.

    Args:
        serial_module: Pyserial-compatible module or test double.
        command_device: Command-port device name.
        baudrate: Serial baud rate.
        index: Zero-based LED index in ``0..6``.
        color: RGB channel values in ``0..255``.

    Returns:
        True when every frame was fully written, otherwise False.
    """
    if (
        isinstance(index, bool)
        or not isinstance(index, int)
        or not 0 <= index < LED_COUNT
    ):
        print(
            "Set-color FAIL — index must be an integer from 0 to 6.",
            file=sys.stderr,
        )
        return False

    if (
        not isinstance(color, tuple)
        or len(color) != 3
        or any(
            isinstance(channel, bool)
            or not isinstance(channel, int)
            or not 0 <= channel <= 255
            for channel in color
        )
    ):
        print(
            "Set-color FAIL — color must be a tuple of three RGB bytes.",
            file=sys.stderr,
        )
        return False

    try:
        with serial_module.Serial(
            command_device,
            baudrate=baudrate,
            timeout=0.1,
            write_timeout=1.0,
        ) as port:
            port.reset_input_buffer()
            port.reset_output_buffer()
            _write_frame(port, build_reset_instructions_frame())

            for led_index in range(LED_COUNT):
                pixel_color = color if led_index == index else BLACK_COLOR
                red, green, blue = pixel_color
                _write_frame(
                    port,
                    build_set_single_color_frame(
                        red,
                        green,
                        blue,
                        led_index,
                        mark=0,
                    ),
                )

            _write_frame(port, build_run_frame())
    except Exception as exc:
        print(f"Set-color FAIL — {exc}.", file=sys.stderr)
        return False

    hex_color = "".join(f"{channel:02X}" for channel in color)
    print(
        f"LED {index} set to #{hex_color} on {command_device}; "
        "all other LEDs set to black. The one-way protocol cannot confirm "
        "controller acceptance."
    )
    return True


def parse_args() -> argparse.Namespace:
    """Parse the LED index, RGB color, and optional command-port override."""
    parser = argparse.ArgumentParser(
        description="Set one prism-kit LED color and turn the other LEDs off."
    )
    parser.add_argument(
        "index",
        type=parse_led_index,
        metavar="INDEX",
        help="Zero-based LED index, from 0 to 6.",
    )
    parser.add_argument(
        "color",
        type=parse_rgb_color,
        metavar="RRGGBB",
        help="Six-digit RGB hex color, optionally prefixed with #.",
    )
    parser.add_argument(
        "--port",
        help="Command serial port; auto-detected for one Prism Kit board if omitted.",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=DEFAULT_BAUDRATE,
        help="Serial baud rate. Default: %(default)s.",
    )
    return parser.parse_args()


def main() -> int:
    """Set one LED from the command line.

    Returns:
        0 on success, 1 on port discovery or serial write failure, or 2 when
        pyserial is unavailable.
    """
    args = parse_args()

    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        print(
            "Set-color FAIL — pyserial is required. Run `uv sync` from prism-kit.",
            file=sys.stderr,
        )
        return 2

    command_device = args.port
    if command_device is None:
        try:
            from serial.tools import list_ports  # type: ignore[import-not-found]

            command_device = resolve_command_port(list_ports)
        except Exception as exc:
            print(f"Set-color port discovery FAIL — {exc}.", file=sys.stderr)
            return 1

    return (
        0
        if run_set_color(
            serial,
            command_device,
            args.baudrate,
            args.index,
            args.color,
        )
        else 1
    )


def cli() -> int:
    """Run the set-color command with stdout and stderr logging."""
    if not __package__:
        sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from scripts.common.cli_logging import run_cli

    return run_cli("set-color", main)


if __name__ == "__main__":
    raise SystemExit(cli())
