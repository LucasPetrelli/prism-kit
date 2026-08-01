#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Rainbow LED chase with device-side delays for prism-kit.

Programs the *entire* sequence of SetMultipleColor, SetSingleColor, and
Delay instructions into the controller's queue, then sends a single Run
to start execution.  The device handles all timing internally — no Python
sleep is needed between steps.

Usage::

    uv run rainbow2 --port COM8
    uv run rainbow2 --port COM8 --step-delay 200
"""

from __future__ import annotations

import argparse
import sys

sys.path.insert(0, str(__import__("pathlib").Path(__file__).resolve().parent.parent))

from scripts.modules.protocol import (  # noqa: E402
    build_reset_instructions_frame,
    build_run_frame,
    build_set_multiple_color_frame,
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


def run_rainbow2_sequence(
    serial_module: object,
    command_device: str,
    baudrate: int,
    step_delay_ms: int = 300,
) -> bool:
    """Program and run the rainbow chase entirely on-device.

    Sends the full instruction sequence in a single burst — turn all LEDs
    off, then light each LED in turn with a delay before the next
    instruction.  A final ``Run`` frame triggers autonomous execution on
    the device.

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

            total_leds = len(RAINBOW_COLORS)

            # ── Queue the full sequence ────────────────────────────
            port.write(build_reset_instructions_frame())

            # SetRange: all LEDs to off (black) at mark=0.
            port.write(build_set_multiple_color_frame(0, 0, 0, 0, total_leds, mark=0))

            for led_idx in range(total_leds):
                r, g, b = RAINBOW_COLORS[led_idx]
                # Each step fires at its own mark (abs ms).  The device handles
                # all timing internally — no host-side sleep needed.
                mark = led_idx * step_delay_ms
                port.write(build_set_single_color_frame(r, g, b, led_idx, mark=mark))

            # ── Go ─────────────────────────────────────────────────
            port.write(build_run_frame())

        print(
            f"Rainbow2 sequence programmed ({total_leds} LEDs, "
            f"{step_delay_ms} ms per step)."
        )
        return True
    except Exception as exc:
        print(f"Rainbow2 sequence FAIL — {exc}.", file=sys.stderr)
        return False


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for the rainbow2 script.

    Returns:
        Parsed arguments with ``--port``, ``--baudrate``, and
        ``--step-delay`` flags.
    """
    parser = argparse.ArgumentParser(
        description="Run rainbow LED chase on prism-kit with device-side delays."
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
    """Run the rainbow2 LED chase sequence.

    Returns:
        0 on success, 1 on failure.
    """
    args = parse_args()

    import serial  # type: ignore[import-not-found]

    ok = run_rainbow2_sequence(serial, args.port, args.baudrate, args.step_delay)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
