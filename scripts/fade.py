#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Program a one-shot red fade for the seven prism-kit LEDs.

Sets all LEDs to red, fades them to 25% over five seconds, then applies a
400% fade over five seconds to restore full brightness.

Usage::

    uv run fade --port COM6
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
    build_set_multiple_color_grouped_frame,
    build_start_frame,
)
from scripts.rainbow2 import RAINBOW_COLORS  # noqa: E402

ROOT_GROUP_ID = 0
ROOT_ADDITIONAL_REPEATS = 0xFFFF
LED_COUNT = len(RAINBOW_COLORS)
RED_COLOR = (0, 0, 128)
FADE_DURATION_MS = 1000
DIM_PERCENT = 5
RESTORE_PERCENT = 2000


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


def run_fade_sequence(serial_module: Any, command_device: str, baudrate: int) -> bool:
    """Program the seven red LEDs to fade down and back up once.

    The program fills the seven-pixel range with red, fades each pixel to 25%
    over 5,000 ms, then applies a 400% fade over the next 5,000 ms. A successful
    write does not confirm controller acceptance because the command protocol
    has no response frame.

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
            _write_frame(
                port,
                build_set_multiple_color_grouped_frame(
                    *RED_COLOR,
                    0,
                    LED_COUNT,
                    group_id=ROOT_GROUP_ID,
                    mark=0,
                ),
            )

            for index in range(LED_COUNT):
                _write_frame(
                    port,
                    build_fade_grouped_frame(
                        0,
                        DIM_PERCENT,
                        FADE_DURATION_MS,
                        index,
                        ROOT_GROUP_ID,
                    ),
                )

            for index in range(LED_COUNT):
                _write_frame(
                    port,
                    build_fade_grouped_frame(
                        FADE_DURATION_MS,
                        RESTORE_PERCENT,
                        FADE_DURATION_MS,
                        index,
                        ROOT_GROUP_ID,
                    ),
                )

            _write_frame(port, build_start_frame(ROOT_ADDITIONAL_REPEATS))
    except Exception as exc:
        print(f"Fade sequence FAIL — {exc}.", file=sys.stderr)
        return False

    print(
        f"Fade sequence programmed ({LED_COUNT} red LEDs, 5 s down to 25%, "
        "5 s back to 100%). The one-way protocol cannot confirm controller "
        "acceptance."
    )
    return True


def parse_args() -> argparse.Namespace:
    """Parse the command port and baud rate for the fade script."""
    parser = argparse.ArgumentParser(
        description="Fade all seven prism-kit LEDs red and back to full brightness."
    )
    parser.add_argument(
        "--port",
        required=True,
        help="Command serial port, for example COM6.",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=115200,
        help="Serial baud rate. Default: %(default)s.",
    )
    return parser.parse_args()


def main() -> int:
    """Run the fade sequence from the command line.

    Returns:
        0 on success, 1 on serial or frame-write failure.
    """
    args = parse_args()

    import serial  # type: ignore[import-not-found]

    ok = run_fade_sequence(serial, args.port, args.baudrate)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
