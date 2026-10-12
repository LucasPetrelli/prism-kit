#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Program the independently repeating, grouped Rainbow4 fade sequence.

Sends the rainbow palette, seven child-group activations, and fourteen
two-second Fade instructions as one fixed 30-frame program burst.

Usage::

    uv run rainbow4 --port COM8
    uv run rainbow4 --port COM8 --stop
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any

_REPO_ROOT = str(Path(__file__).resolve().parent.parent)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from scripts.modules.protocol import (  # noqa: E402
    build_fade_grouped_frame,
    build_reset_instructions_frame,
    build_run_group_frame,
    build_set_single_color_grouped_frame,
    build_start_frame,
)
from scripts.rainbow2 import RAINBOW_COLORS  # noqa: E402

ROOT_GROUP_ID = 0
ROOT_ADDITIONAL_REPEATS = 0
FOREVER_REPEATS = 0xFFFF
FADE_DURATION_MS = 200
DIM_PERCENT = 33
BRIGHTEN_PERCENT = int(10000 / DIM_PERCENT)

ROOT_MARKS = tuple(round(index * 2000 / len(RAINBOW_COLORS)) for index in range(7))


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


def run_rainbow4_sequence(
    serial_module: Any, command_device: str, baudrate: int
) -> bool:
    """Program and start the complete Rainbow4 sequence.

    The 30-frame burst contains reset, 28 grouped instructions, and Start(0).
    A successful write does not confirm controller acceptance because this
    one-way command protocol has no response frame.

    Args:
        serial_module: Pyserial-compatible module or test double.
        command_device: Command-port device name.
        baudrate: Serial baud rate.

    Returns:
        True when every frame was fully written, otherwise False.
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

            for index, (color, mark) in enumerate(
                zip(RAINBOW_COLORS, ROOT_MARKS, strict=True)
            ):
                red, green, blue = color
                _write_frame(
                    port,
                    build_set_single_color_grouped_frame(
                        red,
                        green,
                        blue,
                        index,
                        group_id=ROOT_GROUP_ID,
                        mark=mark,
                    ),
                )
                _write_frame(
                    port,
                    build_run_group_frame(
                        ROOT_GROUP_ID,
                        mark,
                        index + 1,
                        FOREVER_REPEATS,
                    ),
                )

            for index in range(len(RAINBOW_COLORS)):
                child_group_id = index + 1
                _write_frame(
                    port,
                    build_fade_grouped_frame(
                        0,
                        DIM_PERCENT,
                        FADE_DURATION_MS,
                        index,
                        child_group_id,
                    ),
                )
                _write_frame(
                    port,
                    build_fade_grouped_frame(
                        FADE_DURATION_MS,
                        BRIGHTEN_PERCENT,
                        FADE_DURATION_MS,
                        index,
                        child_group_id,
                    ),
                )

            _write_frame(port, build_start_frame(ROOT_ADDITIONAL_REPEATS))
    except Exception as exc:
        print(f"Rainbow4 sequence FAIL — {exc}.", file=sys.stderr)
        return False

    print(
        "Rainbow4 sequence programmed (30 frames). "
        "The one-way protocol cannot confirm controller acceptance."
    )
    return True


def reset_rainbow4_program(
    serial_module: Any, command_device: str, baudrate: int
) -> bool:
    """Send ResetInstructions to stop the active program.

    Reset leaves the currently staged LED colors unchanged and the one-way
    protocol cannot confirm controller acceptance.

    Args:
        serial_module: Pyserial-compatible module or test double.
        command_device: Command-port device name.
        baudrate: Serial baud rate.

    Returns:
        True when the complete reset frame was written, otherwise False.
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
        print(f"Rainbow4 stop FAIL — {exc}.", file=sys.stderr)
        return False

    print(
        "Rainbow4 stop reset frame sent; current LED colors are unchanged. "
        "The one-way protocol cannot confirm controller acceptance."
    )
    return True


def parse_args() -> argparse.Namespace:
    """Parse Rainbow4 command-line arguments.

    Returns:
        Parsed command port, baud rate, and stop selection.
    """
    parser = argparse.ArgumentParser(
        description="Program or stop the grouped Rainbow4 fade sequence."
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
        "--stop",
        action="store_true",
        help="Send ResetInstructions to stop the active program.",
    )
    return parser.parse_args()


def main() -> int:
    """Run or stop Rainbow4 from the command line.

    Returns:
        0 on success, 1 on serial or frame-write failure.
    """
    args = parse_args()

    import serial  # type: ignore[import-not-found]

    if args.stop:
        ok = reset_rainbow4_program(serial, args.port, args.baudrate)
    else:
        ok = run_rainbow4_sequence(serial, args.port, args.baudrate)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
