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
import threading
import time

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
    extract_interface_number,
    format_missing_markers,
    group_ports_by_device,
    print_captured_lines,
)
from scripts import rainbow

DEFAULT_BAUDRATE = 115200
DEFAULT_WAIT_FOR_PORT_SECONDS = 10.0
DEFAULT_CAPTURE_TIMEOUT_SECONDS = 12.0
DEFAULT_PORT_MATCH_TOKENS = ("Prism Kit",)
DEFAULT_DEBUG_MARKER = "Task app_hw runtime:"
DEFAULT_OPTIONAL_MARKERS = ("Booting Zephyr OS build",)
DEFAULT_REQUIRED_MARKERS = (
    "Task app_hw runtime:",
    "Task app_main runtime:",
)
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
        help="Do not require the built-in prism-kit debug marker.",
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
    parser.add_argument(
        "--skip-rainbow",
        action="store_true",
        help="Skip the visual LED rainbow sequence test.",
    )
    parser.add_argument(
        "--rainbow-step-delay",
        type=int,
        default=100,
        help=(
            "Milliseconds between rainbow sequence steps. " "Default: %(default)s ms."
        ),
    )
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def get_port_match_tokens(args: argparse.Namespace) -> list[str]:
    tokens = list(DEFAULT_PORT_MATCH_TOKENS)
    tokens.extend(args.match_port)
    return [token for token in tokens if token]


# ── Port-role resolution by USB interface number ───────────────────────


def resolve_roles_by_interface(
    ports: Sequence[PortInfo],
) -> dict[str, str | None]:
    """Assign debug/command roles based on USB data-interface numbers.

    Within each USB device (grouped by serial number or VID/PID), the
    port with the lowest data-interface number is assigned the ``"debug"``
    role and the next-lowest gets ``"command"``.  This mirrors the device
    tree convention where ``cdc_acm_uart0`` (debug) registers before
    ``cdc_acm_uart1`` (command).

    When interface numbers cannot be extracted for every port in a device
    group, the heuristic falls back to sorting by device name (e.g. COM
    port number on Windows), which typically follows enumeration order.

    Returns a dict with keys ``"debug"`` and ``"command"`` mapping to the
    resolved device paths, or ``None`` when a role could not be resolved.
    """
    import re

    roles: dict[str, str | None] = {"debug": None, "command": None}

    for _device_key, device_ports in group_ports_by_device(ports).items():
        iface_by_device = {p.device: extract_interface_number(p) for p in device_ports}

        # Only trust interface-number ordering when ALL ports in the group
        # have a known interface number.  Otherwise the asymmetry can
        # produce incorrect assignments (a port with a known interface
        # number sorts before one without, even when the latter should
        # come first).
        if all(v is not None for v in iface_by_device.values()) and iface_by_device:
            sort_key = lambda p: iface_by_device.get(p.device, 9999)  # type: ignore[arg-type,return-value]
        else:
            # Fall back to device-name ordering (COM port number on
            # Windows, /dev/ttyACM{N} on Linux).
            def _device_sort_key(p: PortInfo) -> tuple[int, int]:
                nums = re.findall(r"\d+", p.device)
                return (0, int(nums[-1])) if nums else (1, 0)

            sort_key = _device_sort_key

        sorted_ports = sorted(device_ports, key=sort_key)
        if len(sorted_ports) >= 1 and roles["debug"] is None:
            roles["debug"] = sorted_ports[0].device
        if len(sorted_ports) >= 2 and roles["command"] is None:
            roles["command"] = sorted_ports[1].device

    return roles


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
            self._resolve_roles_by_interface()
            self._capture_console()
        except Exception as exc:
            print(exc, file=sys.stderr)
            return 1

        if not self._validate_markers():
            return 1
        if not self._validate_roles():
            return 1

        self._report_success()

        loopback_ok = self._run_loopback()

        rainbow_ok = True
        if not self.args.skip_rainbow:
            rainbow_ok = self._run_rainbow_sequence()

        # Always dump the debug console content at the end — it may
        # contain clues about why the loopback test or rainbow sequence
        # succeeded or failed.
        self._dump_debug_console()

        if not loopback_ok or not rainbow_ok:
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

    # ── Phase: resolve roles by interface number ───────────────────

    def _resolve_roles_by_interface(self) -> None:
        """Resolve debug/command roles from USB interface numbers.

        This fast path avoids opening ports and listening for console
        markers.  If either role cannot be resolved this way, the
        subsequent :meth:`_capture_console` phase will fill in the
        remaining roles from console output (the historical fallback).
        """
        if self.args.port and self.args.command_port:
            # Both roles are already explicit — nothing to resolve.
            self.role_ports["debug"] = self.args.port
            self.role_ports["command"] = self.args.command_port
            return

        if self.args.port:
            self.role_ports["debug"] = self.args.port

        roles = resolve_roles_by_interface(self.ports)
        if roles["debug"] is not None and "debug" not in self.role_ports:
            self.role_ports["debug"] = roles["debug"]
        if roles["command"] is not None and "command" not in self.role_ports:
            self.role_ports["command"] = roles["command"]

        # Report how each role was resolved
        for role, device in (
            ("debug", self.role_ports.get("debug")),
            ("command", self.role_ports.get("command")),
        ):
            if device is None:
                continue
            port = next((p for p in self.ports if p.device == device), None)
            if port is None:
                continue
            iface = extract_interface_number(port)
            if iface is not None:
                print(f"Resolved {role} port by USB interface #{iface}: {device}")
            else:
                print(f"Resolved {role} port by device order: {device}")

    # ── Phase: capture console ─────────────────────────────────────

    def _capture_console(self) -> None:
        required_markers = [str(m) for m in self.args.require]
        if not self.args.no_default_requirements:
            required_markers.extend(DEFAULT_REQUIRED_MARKERS)
        (
            self.captured_lines,
            seen_required,
            self.seen_optional,
            console_roles,
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
            command_marker="",
            quiet=self.args.quiet,
        )
        self.seen_required = seen_required
        # Merge console-captured roles into any roles already resolved by
        # interface number.  Interface-number resolution takes priority.
        for role in ("debug", "command"):
            if role not in self.role_ports and role in console_roles:
                self.role_ports[role] = console_roles[role]

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
        """Validate that the debug port was identified.

        The command port is resolved later by exclusion (the non-debug port)
        rather than by a dedicated banner.
        """
        if self.args.no_default_requirements:
            return True

        if "debug" not in self.role_ports:
            print(
                "Smoke test failed. Debug port marker was not observed.",
                file=sys.stderr,
            )
            if any(self.captured_lines.values()):
                print("Captured console output:", file=sys.stderr)
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

    # ── Phase: dump debug console ──────────────────────────────────

    def _dump_debug_console(self) -> None:
        """Print the captured debug port output for diagnostics."""
        debug_device = self.role_ports.get("debug")
        if debug_device is None:
            return
        lines = self.captured_lines.get(debug_device)
        if not lines:
            print("Debug console: (no output captured)")
            return
        print("Debug console output:")
        for line in lines:
            print(f"  {line}")

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

    # ── Phase: run rainbow sequence ────────────────────────────────

    def _run_rainbow_sequence(self) -> bool:
        """Run the visual rainbow LED chase while capturing debug output.

        Delegates the command-port frame sequence to
        :func:`rainbow.run_rainbow_sequence` while a background thread
        drains the debug port so firmware log output is captured.
        """
        command_device = self._resolve_command_device()
        if not command_device:
            print(
                "Smoke test — no command port identified for rainbow sequence.",
                file=sys.stderr,
            )
            if self.args.no_default_requirements:
                print(
                    "Note: use --command-port to specify the command port explicitly.",
                    file=sys.stderr,
                )
            return False

        debug_device = self.role_ports.get("debug")
        captured: list[str] = []
        reader_error: Exception | None = None
        stop_reader = threading.Event()

        def _read_debug() -> None:
            """Background thread: drain the debug port until signalled."""
            nonlocal reader_error
            if not debug_device:
                return
            try:
                with self.serial.Serial(
                    debug_device,
                    baudrate=self.args.baudrate,
                    timeout=0.05,
                ) as port:
                    port.reset_input_buffer()
                    buf = b""
                    while not stop_reader.is_set():
                        chunk = port.read(64)
                        if chunk:
                            buf += chunk
                            while b"\n" in buf:
                                raw_line, buf = buf.split(b"\n", 1)
                                line = raw_line.decode(
                                    "utf-8", errors="replace"
                                ).rstrip("\r")
                                if line:
                                    captured.append(line)
            except self.serial.SerialException as exc:
                reader_error = exc

        reader = threading.Thread(target=_read_debug, daemon=True)
        reader.start()

        try:
            if not rainbow.run_rainbow_sequence(
                self.serial,
                command_device,
                self.args.baudrate,
                step_delay_ms=self.args.rainbow_step_delay,
            ):
                print(
                    "Smoke test failed — rainbow sequence did not succeed.",
                    file=sys.stderr,
                )
                return False
        finally:
            stop_reader.set()
            reader.join(timeout=1.0)

        if reader_error is not None:
            print(
                f"Rainbow sequence — debug reader error: {reader_error}.",
                file=sys.stderr,
            )

        if captured:
            print("Debug output during rainbow sequence:")
            for line in captured:
                print(f"  {line}")
            if debug_device:
                self.captured_lines.setdefault(debug_device, []).extend(captured)

        return True


# ── Entry point ─────────────────────────────────────────────────────────


def main() -> int:
    return SmokeTest(parse_args()).run()


if __name__ == "__main__":
    raise SystemExit(main())
