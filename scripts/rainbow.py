#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Standalone rainbow LED chase for prism-kit.

Sends ``SetSingleColor`` frames over the command serial port to light up
the WS2812 strip one pixel at a time in rainbow colours.  No debug port
required — use a parallel terminal monitor if you want console output.

Usage::

    uv run rainbow --port COM8
    uv run rainbow --port COM8 --step-delay 200
"""

from __future__ import annotations

import argparse
import sys
import time

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent.parent))

from scripts.modules.protocol import (  # noqa: E402
    build_reset_instructions_frame,
    build_run_frame,
    build_set_single_color_frame,
)

# ── Rainbow colours ────────────────────────────────────────────────────
#
# Mirrors ``kRainbowColors`` in ``app/src/blink_app.cpp`` — 7 WS2812 LEDs
# with green components attenuated for perceptual balance.

RAINBOW_COLORS: list[tuple[int, int, int]] = [
    (255, 0, 0),  # Red
    (255, 40, 0),  # Orange
    (180, 60, 0),  # Yellow
    (0, 80, 0),  # Green
    (0, 0, 255),  # Blue
    (40, 0, 160),  # Indigo
    (140, 0, 255),  # Violet
]


def run_rainbow_sequence(
    serial_module: object,
    command_device: str,
    baudrate: int,
    step_delay_ms: int = 500,
) -> bool:
    """Light up the LED strip one pixel at a time in rainbow colours.

    Each step re-sends all 7 pixels so no stale colour sticks.  Pixels at
    or below the current step get their rainbow colour; the rest are
    explicitly turned off (black).

    Returns *True* on success, prints to stderr and returns *False* on
    failure.
    """
    try:
        with serial_module.Serial(  # type: ignore[attr-defined]
            command_device,
            baudrate=baudrate,
            timeout=0.1,
            write_timeout=1.0,
        ) as port:
            port.reset_input_buffer()
            port.reset_output_buffer()

            step_delay = step_delay_ms / 1000.0
            total_leds = len(RAINBOW_COLORS)

            for step in range(total_leds):
                port.write(build_reset_instructions_frame())

                for led_idx in range(total_leds):
                    if led_idx <= step:
                        r, g, b = RAINBOW_COLORS[led_idx]
                    else:
                        r, g, b = 0, 0, 0
                    port.write(build_set_single_color_frame(r, g, b, led_idx))

                port.write(build_run_frame())
                time.sleep(step_delay)

        print("Rainbow sequence complete.")
        return True
    except Exception as exc:
        print(f"Rainbow sequence FAIL — {exc}.", file=sys.stderr)
        return False


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for the rainbow script.

    Returns:
        Parsed arguments with ``--port``, ``--baudrate``, and
        ``--step-delay`` flags.
    """
    parser = argparse.ArgumentParser(
        description="Run rainbow LED chase on prism-kit command port."
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
        "--step-delay",
        type=int,
        default=300,
        help="Milliseconds between rainbow sequence steps. Default: %(default)s ms.",
    )
    return parser.parse_args()


def main() -> int:
    """Run the rainbow LED chase sequence.

    Returns:
        0 on success, 1 on failure.
    """
    args = parse_args()

    import serial  # type: ignore[import-not-found]

    ok = run_rainbow_sequence(serial, args.port, args.baudrate, args.step_delay)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
