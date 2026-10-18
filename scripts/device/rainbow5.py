#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Program the phase-shifted, circulating Rainbow5 seven-pixel effect.

Usage::

    uv run rainbow5 --port COM8
    uv run rainbow5 --port COM8 --stop

Optional mapping override::

    uv run rainbow5 --port COM8 --ring-indices 4 1 6 0 2 5 --center-index 3
"""

from __future__ import annotations

import argparse
import colorsys
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Any

_REPO_ROOT = str(Path(__file__).resolve().parents[2])
if not __package__ and _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from scripts.device.modules.protocol import (  # noqa: E402
    build_gradient_grouped_frame,
    build_reset_instructions_frame,
    build_start_frame,
)

ROOT_GROUP_ID = 0
ROOT_ADDITIONAL_REPEATS = 0xFFFF
LED_COUNT = 7
RING_POSITION_COUNT = 6
RING_DURATION_MS = 1500
RING_MARKS = (0, 1500, 3000, 4500)
DEFAULT_RING_INDICES = (0, 1, 2, 3, 4, 5)
DEFAULT_CENTER_INDEX = 6
CENTER_WARM_WHITE = (0xFF, 0xF0, 0xD6)
CENTER_LAVENDER = (0xC8, 0xB8, 0xFF)
CENTER_DURATION_MS = 3000


@dataclass(frozen=True)
class Rainbow5Mapping:
    """Map six ordered ring positions and one center to physical LED indices."""

    ring_indices: Sequence[int]
    center_index: int


def validate_mapping(
    ring_indices: Sequence[int], center_index: int
) -> tuple[tuple[int, ...], int]:
    """Validate and return the ordered ring mapping and remaining center index.

    Args:
        ring_indices: LED indices ordered by logical ring position.
        center_index: LED index assigned to the center pixel.

    Returns:
        The validated ordered ring indices and center index.

    Raises:
        ValueError: If the seven indices are not distinct integers in 0..6.
    """
    try:
        ordered_ring = tuple(ring_indices)
    except TypeError as exc:
        raise ValueError("ring mapping must contain six LED indices") from exc

    if len(ordered_ring) != RING_POSITION_COUNT:
        raise ValueError("ring mapping must contain exactly six LED indices")
    if any(type(index) is not int for index in ordered_ring):
        raise ValueError("ring indices must be integers")
    if type(center_index) is not int:
        raise ValueError("center index must be an integer")

    indices = (*ordered_ring, center_index)
    if any(index < 0 or index >= LED_COUNT for index in indices):
        raise ValueError("all LED indices must be in 0..6")
    if len(set(ordered_ring)) != RING_POSITION_COUNT:
        raise ValueError("ring indices must be distinct")
    if center_index in ordered_ring:
        raise ValueError("center index must be the remaining LED index")
    if set(indices) != set(range(LED_COUNT)):
        raise ValueError("ring and center mapping must cover LED indices 0..6")

    return ordered_ring, center_index


def _hsv_to_srgb(hue_degrees: int) -> tuple[int, int, int]:
    """Convert a full-saturation, 75%-value HSV hue to sRGB bytes."""
    red, green, blue = colorsys.hsv_to_rgb(hue_degrees / 360.0, 1.0, 0.75)
    return (round(red * 255), round(green * 255), round(blue * 255))


def _build_frames(
    ring_indices: tuple[int, ...], center_index: int
) -> list[bytes]:
    """Build Reset, all 26 grouped Gradient frames, and Start."""
    frames = [build_reset_instructions_frame()]

    # Build each of the four root-relative ring transition waypoints in order.
    for waypoint, mark in enumerate(RING_MARKS):
        # Every logical ring position gets its phase-shifted destination color.
        for logical_position, led_index in enumerate(ring_indices):
            hue = (60 * logical_position - 90 * waypoint) % 360
            red, green, blue = _hsv_to_srgb(hue)
            frames.append(
                build_gradient_grouped_frame(
                    mark,
                    red,
                    green,
                    blue,
                    RING_DURATION_MS,
                    led_index,
                    ROOT_GROUP_ID,
                )
            )

        # The center transitions only at the two alternating-color marks.
        if mark == 0:
            center_color = CENTER_WARM_WHITE
            center_mark = 0
        elif mark == 3000:
            center_color = CENTER_LAVENDER
            center_mark = 3000
        else:
            continue

        frames.append(
            build_gradient_grouped_frame(
                center_mark,
                *center_color,
                CENTER_DURATION_MS,
                center_index,
                ROOT_GROUP_ID,
            )
        )

    frames.append(build_start_frame(ROOT_ADDITIONAL_REPEATS))
    return frames


def build_rainbow5_frames(
    ring_indices: Sequence[int], center_index: int
) -> list[bytes]:
    """Build the complete validated Rainbow5 command-frame sequence.

    Args:
        ring_indices: LED indices ordered by logical ring position.
        center_index: LED index assigned to the center pixel.

    Returns:
        Reset, 26 grouped Gradient, and Start protocol frames.
    """
    ordered_ring, validated_center = validate_mapping(ring_indices, center_index)
    return _build_frames(ordered_ring, validated_center)


def _write_frame(port: Any, frame: bytes) -> None:
    """Write one complete protocol frame or raise on a short serial write."""
    written = port.write(frame)
    if written != len(frame):
        raise OSError(f"short serial write ({written}/{len(frame)} bytes)")


def run_rainbow5_sequence(
    serial_module: Any,
    command_device: str,
    baudrate: int,
    mapping: Rainbow5Mapping,
) -> bool:
    """Validate the mapping, then program the repeating seven-pixel effect.

    Mapping validation and frame construction finish before opening the serial
    device. Successful writes do not confirm controller acceptance because
    the command protocol is one-way.

    Args:
        serial_module: Pyserial-compatible module or test double.
        command_device: Command-port device name.
        baudrate: Serial baud rate.
        mapping: LED indices ordered by logical ring position and the center.

    Returns:
        True when every frame is fully written, otherwise False.
    """
    try:
        frames = build_rainbow5_frames(
            mapping.ring_indices, mapping.center_index
        )
    except (TypeError, ValueError) as exc:
        print(f"Rainbow5 mapping FAIL — {exc}.", file=sys.stderr)
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
            for frame in frames:
                _write_frame(port, frame)
    except Exception as exc:
        print(f"Rainbow5 sequence FAIL — {exc}.", file=sys.stderr)
        return False

    print(
        "Rainbow5 sequence programmed (28 frames; nominal 6000 ms cycle). "
        "The one-way protocol cannot confirm controller acceptance."
    )
    return True


def reset_rainbow5_program(
    serial_module: Any, command_device: str, baudrate: int
) -> bool:
    """Send Reset only; the currently displayed LED colors remain in place.

    Args:
        serial_module: Pyserial-compatible module or test double.
        command_device: Command-port device name.
        baudrate: Serial baud rate.

    Returns:
        True when the complete Reset frame is written, otherwise False.
    """
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
    except Exception as exc:
        print(f"Rainbow5 stop FAIL — {exc}.", file=sys.stderr)
        return False

    print(
        "Rainbow5 stop reset frame sent; current LED colors are unchanged. "
        "The one-way protocol cannot confirm controller acceptance."
    )
    return True


def parse_args() -> argparse.Namespace:
    """Parse serial settings and the optional seven-pixel mapping.

    Returns:
        Parsed port, baud rate, mapping, and stop selection.
    """
    parser = argparse.ArgumentParser(
        description="Program or stop the phase-shifted Rainbow5 effect."
    )
    parser.add_argument(
        "--port",
        required=True,
        help="Command serial port, for example COM8.",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=115200,
        help="Serial baud rate. Default: %(default)s.",
    )
    parser.add_argument(
        "--ring-indices",
        nargs=RING_POSITION_COUNT,
        type=int,
        default=DEFAULT_RING_INDICES,
        metavar="INDEX",
        help=(
            "LED indices in logical ring order, from position 0 to 5. "
            "Default: 0 1 2 3 4 5."
        ),
    )
    parser.add_argument(
        "--center-index",
        type=int,
        default=DEFAULT_CENTER_INDEX,
        help="LED index for the center pixel. Default: 6.",
    )
    parser.add_argument(
        "--stop",
        action="store_true",
        help="Send Reset only to stop the active program.",
    )
    args = parser.parse_args()
    return args


def main() -> int:
    """Run Rainbow5 or send the Reset-only stop command.

    Returns:
        0 on success, 1 on serial or frame-write failure.
    """
    args = parse_args()

    import serial  # type: ignore[import-not-found]

    if args.stop:
        ok = reset_rainbow5_program(serial, args.port, args.baudrate)
    else:
        ok = run_rainbow5_sequence(
            serial,
            args.port,
            args.baudrate,
            Rainbow5Mapping(args.ring_indices, args.center_index),
        )
    return 0 if ok else 1


def cli() -> int:
    """Run the rainbow5 command with stdout and stderr logging."""
    if not __package__:
        sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from scripts.common.cli_logging import run_cli

    return run_cli("rainbow5", main)


if __name__ == "__main__":
    raise SystemExit(cli())
