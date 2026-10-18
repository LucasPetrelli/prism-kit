#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Program one grouped Gradient transition on a prism-kit LED.

Usage::

    uv run gradient --port COM8 --index 3 --target 0000FF --duration-ms 1500
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

_REPO_ROOT = str(Path(__file__).resolve().parent.parent)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from scripts.modules.protocol import (  # noqa: E402
    build_gradient_grouped_frame,
    build_reset_instructions_frame,
    build_start_frame,
)

LED_COUNT = 7
DEFAULT_BAUDRATE = 115200
ROOT_GROUP_ID = 0
ROOT_ADDITIONAL_REPEATS = 0
_HEX_COLOR_PATTERN = re.compile(r"[0-9A-Fa-f]{6}")


def parse_led_index(value: str) -> int:
    """Parse a zero-based LED index in the supported range.

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
    """Parse a six-digit target color in RRGGBB order.

    Args:
        value: Six hexadecimal digits, optionally prefixed with ``#``.

    Returns:
        Red, green, and blue channel values in ``0..255``.

    Raises:
        argparse.ArgumentTypeError: If the value is not exactly six hex digits.
    """
    digits = value[1:] if value.startswith("#") else value
    if _HEX_COLOR_PATTERN.fullmatch(digits) is None:
        raise argparse.ArgumentTypeError(
            "target must be six hex digits in RRGGBB format"
        )

    return (
        int(digits[0:2], 16),
        int(digits[2:4], 16),
        int(digits[4:6], 16),
    )


def parse_u16(value: str) -> int:
    """Parse a base-10 unsigned 16-bit value.

    Args:
        value: Text supplied for a mark or duration.

    Returns:
        An integer in ``0..65535``.

    Raises:
        argparse.ArgumentTypeError: If the value is outside the u16 range.
    """
    try:
        parsed = int(value, 10)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("value must be an integer from 0 to 65535") from exc

    if not 0 <= parsed <= 0xFFFF:
        raise argparse.ArgumentTypeError("value must be from 0 to 65535")
    return parsed


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


def run_gradient_sequence(
    serial_module: Any,
    command_device: str,
    baudrate: int,
    index: int,
    target: tuple[int, int, int],
    duration_ms: int,
    mark: int = 0,
) -> bool:
    """Program and start a single root-group Gradient transition.

    The sequence resets the queued instructions, adds one grouped Gradient to
    root group 0, and starts that group for one pass. Gradient captures the
    selected pixel's staged color when the mark becomes due. The protocol is
    one-way, so successful serial writes do not confirm controller acceptance.

    Args:
        serial_module: Pyserial-compatible module or test double.
        command_device: Command-port device name.
        baudrate: Serial baud rate.
        index: Zero-based LED index in ``0..6``.
        target: Destination RGB channel values in ``0..255``.
        duration_ms: Transition duration in ``0..65535`` milliseconds.
        mark: Root-relative start mark in ``0..65535`` milliseconds.

    Returns:
        True when every frame was fully written, otherwise False.
    """
    if (
        isinstance(index, bool)
        or not isinstance(index, int)
        or not 0 <= index < LED_COUNT
    ):
        print("Gradient FAIL — index must be an integer from 0 to 6.", file=sys.stderr)
        return False

    if (
        not isinstance(target, tuple)
        or len(target) != 3
        or any(
            isinstance(channel, bool)
            or not isinstance(channel, int)
            or not 0 <= channel <= 255
            for channel in target
        )
    ):
        print(
            "Gradient FAIL — target must be a tuple of three RGB bytes.",
            file=sys.stderr,
        )
        return False

    for value, name in ((duration_ms, "duration"), (mark, "mark")):
        if (
            isinstance(value, bool)
            or not isinstance(value, int)
            or not 0 <= value <= 0xFFFF
        ):
            print(
                f"Gradient FAIL — {name} must be an integer from 0 to 65535.",
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
            _write_frame(
                port,
                build_gradient_grouped_frame(
                    mark,
                    *target,
                    duration_ms,
                    index,
                    ROOT_GROUP_ID,
                ),
            )
            _write_frame(port, build_start_frame(ROOT_ADDITIONAL_REPEATS))
    except Exception as exc:
        print(f"Gradient FAIL — {exc}.", file=sys.stderr)
        return False

    target_hex = "".join(f"{channel:02X}" for channel in target)
    print(
        f"Gradient programmed for LED {index} to #{target_hex} over "
        f"{duration_ms} ms at mark {mark}. The one-way protocol cannot confirm "
        "controller acceptance."
    )
    return True


def parse_args() -> argparse.Namespace:
    """Parse the target LED, color, duration, and command-port options."""
    parser = argparse.ArgumentParser(
        description="Transition one prism-kit LED to an RGB target color."
    )
    parser.add_argument(
        "--port",
        required=True,
        help="Command serial port, for example COM8.",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=DEFAULT_BAUDRATE,
        help="Serial baud rate. Default: %(default)s.",
    )
    parser.add_argument(
        "--index",
        type=parse_led_index,
        required=True,
        help="Zero-based LED index, from 0 to 6.",
    )
    parser.add_argument(
        "--target",
        type=parse_rgb_color,
        required=True,
        metavar="RRGGBB",
        help="Destination RGB color as six hex digits, optionally prefixed with #.",
    )
    parser.add_argument(
        "--duration-ms",
        type=parse_u16,
        required=True,
        help="Transition duration in milliseconds, from 0 to 65535.",
    )
    parser.add_argument(
        "--mark",
        type=parse_u16,
        default=0,
        help="Root-relative start mark in milliseconds. Default: %(default)s.",
    )
    return parser.parse_args()


def main() -> int:
    """Run the Gradient command from the command line.

    Returns:
        0 on success, 1 on serial or frame-write failure, or 2 if pyserial is
        unavailable.
    """
    args = parse_args()

    try:
        import serial  # type: ignore[import-not-found]
    except ImportError:
        print(
            "Gradient FAIL — pyserial is required. Run `uv sync` from prism-kit.",
            file=sys.stderr,
        )
        return 2

    return (
        0
        if run_gradient_sequence(
            serial,
            args.port,
            args.baudrate,
            args.index,
            args.target,
            args.duration_ms,
            args.mark,
        )
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
