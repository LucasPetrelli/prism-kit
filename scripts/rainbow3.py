#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Program an indefinitely repeating, device-timed rainbow sequence.

Usage::

    uv run rainbow3 --port COM8
    uv run rainbow3 --port COM8 --step-delay 200
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
    build_reset_instructions_frame,
    build_set_multiple_color_grouped_frame,
    build_set_single_color_grouped_frame,
    build_start_frame,
)
from scripts.rainbow2 import RAINBOW_COLORS  # noqa: E402

DEFAULT_STEP_DELAY_MS = 200
MIN_STEP_DELAY_MS = 1
MAX_STEP_DELAY_MS = 9362
ROOT_REPEAT_FOREVER = 0xFFFF


def parse_step_delay_ms(value: str) -> int:
    """Parse and validate a step delay for an argparse option.

    Args:
        value: Text supplied for a step-delay option.

    Returns:
        The delay in milliseconds when it fits every uint16 instruction mark.

    Raises:
        argparse.ArgumentTypeError: If the value is not an integer in
            ``1..9362``.
    """
    try:
        delay_ms = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("step delay must be an integer") from exc

    if not MIN_STEP_DELAY_MS <= delay_ms <= MAX_STEP_DELAY_MS:
        raise argparse.ArgumentTypeError(
            f"step delay must be between {MIN_STEP_DELAY_MS} and "
            f"{MAX_STEP_DELAY_MS} milliseconds"
        )
    return delay_ms


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


def run_rainbow3_sequence(
    serial_module: Any,
    command_device: str,
    baudrate: int,
    step_delay_ms: int = DEFAULT_STEP_DELAY_MS,
) -> bool:
    """Program the Rainbow2 palette and start Group 0 with forever repeats.

    The controller executes an initial black range, the seven palette colors,
    and a final black range on its own timeline. A successful host write does
    not confirm device acceptance because the command protocol has no response
    frame.

    Args:
        serial_module: Pyserial-compatible module or test double.
        command_device: Command-port device name.
        baudrate: Serial baud rate.
        step_delay_ms: Spacing between LED marks, in ``1..9362`` ms.

    Returns:
        True when every frame was fully written, otherwise False.
    """
    if (
        isinstance(step_delay_ms, bool)
        or not isinstance(step_delay_ms, int)
        or not MIN_STEP_DELAY_MS <= step_delay_ms <= MAX_STEP_DELAY_MS
    ):
        print(
            f"Rainbow3 sequence FAIL — step delay must be an integer between "
            f"{MIN_STEP_DELAY_MS} and {MAX_STEP_DELAY_MS} milliseconds.",
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
                build_set_multiple_color_grouped_frame(
                    0, 0, 0, 0, len(RAINBOW_COLORS), group_id=0, mark=0
                ),
            )

            for index, (red, green, blue) in enumerate(RAINBOW_COLORS):
                _write_frame(
                    port,
                    build_set_single_color_grouped_frame(
                        red,
                        green,
                        blue,
                        index,
                        group_id=0,
                        mark=index * step_delay_ms,
                    ),
                )

            _write_frame(
                port,
                build_set_multiple_color_grouped_frame(
                    0,
                    0,
                    0,
                    0,
                    len(RAINBOW_COLORS),
                    group_id=0,
                    mark=7 * step_delay_ms,
                ),
            )
            _write_frame(port, build_start_frame(ROOT_REPEAT_FOREVER))
    except Exception as exc:
        print(f"Rainbow3 sequence FAIL — {exc}.", file=sys.stderr)
        return False

    print(
        f"Rainbow3 sequence programmed ({len(RAINBOW_COLORS)} LEDs, "
        f"{step_delay_ms} ms per step, Group 0 repeats forever)."
    )
    return True


def reset_rainbow3_program(
    serial_module: Any, command_device: str, baudrate: int
) -> bool:
    """Attempt to abort and clear the running Rainbow3 controller program.

    ResetInstructions leaves the current LED colors unchanged, and the
    one-way protocol cannot confirm that the controller accepted the reset.

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
            _write_frame(port, build_reset_instructions_frame())
    except Exception as exc:
        print(f"Rainbow3 cleanup FAIL — {exc}.", file=sys.stderr)
        return False

    print("Rainbow3 cleanup reset frame sent.")
    return True


def parse_args() -> argparse.Namespace:
    """Parse Rainbow3 command-line arguments.

    Returns:
        Parsed command port, baud rate, and validated step delay.
    """
    parser = argparse.ArgumentParser(
        description="Run an indefinitely repeating rainbow sequence on prism-kit."
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
        type=parse_step_delay_ms,
        default=DEFAULT_STEP_DELAY_MS,
        help="Milliseconds between rainbow steps. Default: %(default)s ms.",
    )
    return parser.parse_args()


def main() -> int:
    """Run the Rainbow3 sequence.

    Returns:
        0 on success, 1 on serial or frame-write failure.
    """
    args = parse_args()

    import serial  # type: ignore[import-not-found]

    ok = run_rainbow3_sequence(
        serial, args.port, args.baudrate, step_delay_ms=args.step_delay
    )
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
