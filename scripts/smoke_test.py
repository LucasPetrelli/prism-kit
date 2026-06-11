#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyserial"]
# ///

"""Smoke test for prism-kit firmware over USB CDC.

Usage::

    uv run smoke-test
    uv run smoke-test --port COM7 --command-port COM8
    uv run smoke-test --list-ports
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

# Ensure the repo root is on sys.path so package imports resolve correctly
# both when running directly (uv run scripts/smoke_test.py) and via entry
# points (uv run smoke-test).
_REPO_ROOT = str(Path(__file__).resolve().parent.parent)
if _REPO_ROOT not in sys.path:
    sys.path.insert(0, _REPO_ROOT)

from scripts.modules import protocol
from scripts.modules.serial_utils import (
    PortInfo,
    as_port_info,
    capture_console,
    describe_port,
    discover_ports,
    format_missing_markers,
    print_captured_lines,
)

DEFAULT_BAUDRATE = 115200
DEFAULT_WAIT_FOR_PORT_SECONDS = 10.0
DEFAULT_CAPTURE_TIMEOUT_SECONDS = 12.0
DEFAULT_PORT_MATCH_TOKENS = ("Prism Kit",)
DEFAULT_DEBUG_MARKER = "DebugPort online on"
DEFAULT_COMMAND_MARKER = "CommandPort online on"
DEFAULT_OPTIONAL_MARKERS = ("Booting Zephyr OS build",)
DEFAULT_LOOPBACK_PAYLOAD = bytes.fromhex("01020304")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Connect to the prism-kit USB CDC ports and verify that the debug "
            "console and command channel enumerate separately."
        )
    )
    parser.add_argument(
        "--port",
        help="Explicit debug serial port to use, for example COM7.",
    )
    parser.add_argument(
        "--command-port",
        help="Explicit command serial port to use, for example COM8.",
    )
    parser.add_argument(
        "--baudrate",
        type=int,
        default=DEFAULT_BAUDRATE,
        help="Serial baud rate passed to the host driver. Default: %(default)s.",
    )
    parser.add_argument(
        "--wait-for-port",
        type=float,
        default=DEFAULT_WAIT_FOR_PORT_SECONDS,
        help=(
            "How long to wait for the CDC ACM device to enumerate before "
            "failing. Default: %(default)s seconds."
        ),
    )
    parser.add_argument(
        "--capture-timeout",
        type=float,
        default=DEFAULT_CAPTURE_TIMEOUT_SECONDS,
        help=(
            "How long to capture console output after the ports open. Default: "
            "%(default)s seconds."
        ),
    )
    parser.add_argument(
        "--match-port",
        action="append",
        default=[],
        metavar="TEXT",
        help=(
            "Additional case-insensitive text that may appear in the port device, "
            "description, product, manufacturer, serial number, or hardware ID."
        ),
    )
    parser.add_argument(
        "--require",
        action="append",
        default=[],
        metavar="TEXT",
        help="Additional case-sensitive console marker that must appear.",
    )
    parser.add_argument(
        "--no-default-requirements",
        action="store_true",
        help="Do not require the built-in prism-kit debug/command markers.",
    )
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="List visible serial ports and exit.",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Do not stream console lines while capturing.",
    )
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def get_port_match_tokens(args: argparse.Namespace) -> list[str]:
    tokens = list(DEFAULT_PORT_MATCH_TOKENS)
    tokens.extend(args.match_port)
    return [token for token in tokens if token]


# ── Smoke test orchestrator ────────────────────────────────────────────


class SmokeTest:
    """Orchestrates the prism-kit smoke test pipeline.

    Each phase of the pipeline is a single-responsibility method.
    ``run()`` calls them in order and returns a process exit code.
    """

    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.serial = None
        self.list_ports = None
        self.ports: list[PortInfo] = []
        self.captured_lines: dict[str, list[str]] = {}
        self.seen_required: set[str] = set()
        self.seen_optional: set[str] = set()
        self.role_ports: dict[str, str] = {}

    # ── Public API ─────────────────────────────────────────────────

    def run(self) -> int:
        """Run the full smoke test pipeline. Returns a process exit code."""
        if not self._import_serial():
            return 2
        if self.args.list_ports:
            self._run_list_ports()
            return 0

        try:
            self._discover_ports()
            self._capture_console()
        except Exception as exc:
            print(exc, file=sys.stderr)
            return 1

        if not self._validate_markers():
            return 1
        if not self._validate_roles():
            return 1

        self._report_success()

        if not self._run_loopback():
            return 1

        return 0

    # ── Phase: import serial library ───────────────────────────────

    def _import_serial(self) -> bool:
        try:
            import serial  # type: ignore[import-not-found]
            from serial.tools import list_ports  # type: ignore[import-not-found]
        except ImportError:
            print(
                "pyserial is required for smoke testing. Install it with:\n"
                "  uv add pyserial\n"
                "  uv run scripts/smoke_test.py",
                file=sys.stderr,
            )
            return False

        self.serial = serial
        self.list_ports = list_ports
        return True

    # ── Phase: list ports ──────────────────────────────────────────

    def _run_list_ports(self) -> None:
        assert self.list_ports is not None  # guarded by _import_serial
        visible = [as_port_info(p) for p in self.list_ports.comports()]
        from scripts.modules.serial_utils import list_visible_ports

        list_visible_ports(visible)

    # ── Phase: discover ports ──────────────────────────────────────

    def _discover_ports(self) -> None:
        tokens = get_port_match_tokens(self.args)
        self.ports = discover_ports(
            self.list_ports,
            port=self.args.port,
            command_port=self.args.command_port,
            wait_for_port=self.args.wait_for_port,
            port_match_tokens=tokens,
        )
        print("Using serial ports:")
        for port in self.ports:
            print(f"- {describe_port(port)}")

    # ── Phase: capture console ─────────────────────────────────────

    def _capture_console(self) -> None:
        required_markers = [str(m) for m in self.args.require]
        (
            self.captured_lines,
            self.seen_required,
            self.seen_optional,
            self.role_ports,
        ) = capture_console(
            self.serial,
            self.ports,
            baudrate=self.args.baudrate,
            capture_timeout=self.args.capture_timeout,
            required_markers=required_markers,
            optional_markers=DEFAULT_OPTIONAL_MARKERS,
            debug_marker=(
                DEFAULT_DEBUG_MARKER if not self.args.no_default_requirements else ""
            ),
            command_marker=(
                DEFAULT_COMMAND_MARKER if not self.args.no_default_requirements else ""
            ),
            quiet=self.args.quiet,
        )

    # ── Phase: validate console markers ────────────────────────────

    def _validate_markers(self) -> bool:
        required_markers = [str(m) for m in self.args.require]
        if not required_markers:
            return True
        if self.seen_required.issuperset(required_markers):
            return True

        print(
            "Smoke test failed. Missing required console markers:\n"
            f"{format_missing_markers(required_markers, self.seen_required)}",
            file=sys.stderr,
        )
        if any(self.captured_lines.values()):
            print("Captured console output:", file=sys.stderr)
            print_captured_lines(self.captured_lines, sys.stderr)
        else:
            print(
                "No console output was captured before the timeout expired.",
                file=sys.stderr,
            )
        return False

    # ── Phase: validate port roles ─────────────────────────────────

    def _validate_roles(self) -> bool:
        if self.args.no_default_requirements:
            return True

        missing_roles: list[str] = []
        if "debug" not in self.role_ports:
            missing_roles.append("debug")
        if "command" not in self.role_ports:
            missing_roles.append("command")
        if missing_roles:
            print(
                "Smoke test failed. Missing required port role markers:\n"
                + "\n".join(f"- {role}" for role in missing_roles),
                file=sys.stderr,
            )
            if any(self.captured_lines.values()):
                print("Captured console output:", file=sys.stderr)
                print_captured_lines(self.captured_lines, sys.stderr)
            return False

        if (
            "command" in self.role_ports
            and self.role_ports["debug"] == self.role_ports["command"]
        ):
            print(
                "Smoke test failed. Debug and command markers were observed on "
                "the same serial port.",
                file=sys.stderr,
            )
            print_captured_lines(self.captured_lines, sys.stderr)
            return False

        return True

    # ── Phase: report success ──────────────────────────────────────

    def _report_success(self) -> None:
        print("Smoke test passed.")
        if not self.args.no_default_requirements:
            print(f"Debug port: {self.role_ports['debug']}")
            if "command" in self.role_ports:
                print(f"Command port: {self.role_ports['command']}")
        if self.seen_optional:
            print("Observed optional markers:")
            for marker in DEFAULT_OPTIONAL_MARKERS:
                if marker in self.seen_optional:
                    print(f"- {marker}")

    # ── Phase: resolve command device ──────────────────────────────

    def _resolve_command_device(self) -> str | None:
        command_device = self.role_ports.get("command")
        if command_device:
            return command_device

        # Fallback: pick a port with a serial number that isn't the debug port.
        return next(
            (
                p.device
                for p in self.ports
                if p.serial_number and p.device != self.role_ports.get("debug")
            ),
            None,
        )

    # ── Phase: run loopback test ───────────────────────────────────

    def _run_loopback(self) -> bool:
        command_device = self._resolve_command_device()
        if not command_device:
            print(
                "Smoke test failed — no command port identified for loopback test.",
                file=sys.stderr,
            )
            if self.args.no_default_requirements:
                print(
                    "Note: use --command-port to specify the command port explicitly.",
                    file=sys.stderr,
                )
            return False

        print(
            f"Sending loopback ({DEFAULT_LOOPBACK_PAYLOAD.hex()}) on "
            f"{command_device} ..."
        )
        if not protocol.run_loopback_test(
            self.serial,
            command_device,
            self.args.baudrate,
            DEFAULT_LOOPBACK_PAYLOAD,
        ):
            print(
                "Smoke test failed — loopback test did not succeed.",
                file=sys.stderr,
            )
            return False

        return True


# ── Entry point ─────────────────────────────────────────────────────────


def main() -> int:
    return SmokeTest(parse_args()).run()


if __name__ == "__main__":
    raise SystemExit(main())
