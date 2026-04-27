#!/usr/bin/env python3

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import subprocess
import sys
import time
from typing import Any, Iterable, Sequence


DEFAULT_BAUDRATE = 115200
DEFAULT_WAIT_FOR_PORT_SECONDS = 10.0
DEFAULT_CAPTURE_TIMEOUT_SECONDS = 12.0
DEFAULT_PORT_MATCH_TOKENS = ("XIAO SAMD21 Debug Console",)
DEFAULT_REQUIRED_MARKERS = ("Task app_main runtime:",)
DEFAULT_OPTIONAL_MARKERS = (
    "Booting Zephyr OS build",
    "DebugPort online on",
)


@dataclass(frozen=True)
class PortInfo:
    device: str
    description: str
    product: str | None
    manufacturer: str | None
    serial_number: str | None
    hwid: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Connect to the sentient-mask USB CDC debug console and verify the "
            "expected runtime markers."
        )
    )
    parser.add_argument(
        "--port",
        help="Explicit serial port to use, for example COM7.",
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
            "How long to capture console output after the port opens. Default: "
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
        help="Do not require the built-in sentient-mask runtime markers.",
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


def repo_python_executable(root: Path) -> Path:
    return root / ".venv" / "Scripts" / "python.exe"


def prefer_repo_python(root: Path) -> None:
    repo_python = repo_python_executable(root)
    if not repo_python.is_file():
        return

    current_python = Path(sys.executable).resolve()
    if current_python == repo_python.resolve():
        return

    completed = subprocess.run([str(repo_python), __file__, *sys.argv[1:]], check=False)
    raise SystemExit(completed.returncode)


def get_required_markers(args: argparse.Namespace) -> list[str]:
    markers: list[str] = []
    if not args.no_default_requirements:
        markers.extend(str(marker) for marker in DEFAULT_REQUIRED_MARKERS)
    markers.extend(args.require)
    return markers


def get_port_match_tokens(args: argparse.Namespace) -> list[str]:
    tokens = list(DEFAULT_PORT_MATCH_TOKENS)
    tokens.extend(args.match_port)
    return [token for token in tokens if token]


def as_port_info(port: object) -> PortInfo:
    return PortInfo(
        device=getattr(port, "device", ""),
        description=getattr(port, "description", "") or "",
        product=getattr(port, "product", None),
        manufacturer=getattr(port, "manufacturer", None),
        serial_number=getattr(port, "serial_number", None),
        hwid=getattr(port, "hwid", "") or "",
    )


def port_fields(port: PortInfo) -> tuple[str, ...]:
    return (
        port.device,
        port.description,
        port.product or "",
        port.manufacturer or "",
        port.serial_number or "",
        port.hwid,
    )


def port_matches_tokens(port: PortInfo, tokens: Sequence[str]) -> bool:
    if not tokens:
        return True

    haystack = "\n".join(field.lower() for field in port_fields(port))
    return any(token.lower() in haystack for token in tokens)


def looks_like_usb_serial_port(port: PortInfo) -> bool:
    haystack = "\n".join(field.lower() for field in port_fields(port))
    return "usb" in haystack or "vid:pid" in haystack or "serial" in haystack


def score_port(port: PortInfo, tokens: Sequence[str]) -> int:
    score = 0
    haystack = [field.lower() for field in port_fields(port)]
    for token in tokens:
        normalized = token.lower()
        if any(normalized == field for field in haystack if field):
            score += 10
        elif any(normalized in field for field in haystack):
            score += 1
    return score


def describe_port(port: PortInfo) -> str:
    details = [port.device]
    if port.description:
        details.append(port.description)
    if port.product and port.product != port.description:
        details.append(f"product={port.product}")
    if port.manufacturer:
        details.append(f"manufacturer={port.manufacturer}")
    if port.serial_number:
        details.append(f"serial={port.serial_number}")
    return " | ".join(details)


def list_visible_ports(port_infos: Sequence[PortInfo]) -> None:
    if not port_infos:
        print("No serial ports detected.")
        return

    for port in port_infos:
        print(describe_port(port))


def resolve_explicit_port(
    port_infos: Sequence[PortInfo], explicit_port: str
) -> PortInfo | None:
    normalized = explicit_port.casefold()
    for port in port_infos:
        if port.device.casefold() == normalized:
            return port
    return None


def resolve_auto_port(
    port_infos: Sequence[PortInfo], tokens: Sequence[str]
) -> PortInfo | None:
    matches = [port for port in port_infos if port_matches_tokens(port, tokens)]
    if not matches:
        usb_serial_ports = [
            port for port in port_infos if looks_like_usb_serial_port(port)
        ]
        if len(usb_serial_ports) == 1:
            return usb_serial_ports[0]
        return None

    ranked = sorted(matches, key=lambda port: score_port(port, tokens), reverse=True)
    if len(ranked) == 1:
        return ranked[0]

    best_score = score_port(ranked[0], tokens)
    best_matches = [port for port in ranked if score_port(port, tokens) == best_score]
    if len(best_matches) == 1:
        return best_matches[0]

    candidates = "\n".join(f"- {describe_port(port)}" for port in best_matches)
    raise RuntimeError(
        "Multiple serial ports match the smoke-test search tokens. Re-run with "
        f"--port to choose one explicitly.\n{candidates}"
    )


def wait_for_port(args: argparse.Namespace, list_ports_module: Any) -> PortInfo:
    deadline = time.monotonic() + args.wait_for_port
    tokens = get_port_match_tokens(args)
    last_snapshot: list[PortInfo] = []

    while time.monotonic() <= deadline:
        port_infos = [as_port_info(port) for port in list_ports_module.comports()]
        last_snapshot = port_infos

        if args.port:
            resolved = resolve_explicit_port(port_infos, args.port)
        else:
            resolved = resolve_auto_port(port_infos, tokens)

        if resolved is not None:
            return resolved

        time.sleep(0.25)

    visible = (
        "\n".join(f"- {describe_port(port)}" for port in last_snapshot) or "- none"
    )
    if args.port:
        raise RuntimeError(
            f"Serial port '{args.port}' did not appear within {args.wait_for_port:.1f} seconds.\n"
            f"Visible ports at timeout:\n{visible}"
        )

    token_text = ", ".join(get_port_match_tokens(args)) or "<any port>"
    raise RuntimeError(
        "No serial port matched the smoke-test discovery tokens within "
        f"{args.wait_for_port:.1f} seconds.\n"
        f"Port match tokens: {token_text}\n"
        f"Visible ports at timeout:\n{visible}"
    )


def update_marker_set(
    line: str, markers: Iterable[str], seen_markers: set[str]
) -> None:
    for marker in markers:
        if marker in line:
            seen_markers.add(marker)


def capture_console(
    args: argparse.Namespace,
    serial_module: Any,
    port: PortInfo,
    required_markers: Sequence[str],
) -> tuple[list[str], set[str], set[str]]:
    captured_lines: list[str] = []
    seen_required: set[str] = set()
    seen_optional: set[str] = set()
    deadline = time.monotonic() + args.capture_timeout

    with serial_module.Serial(
        port.device,
        baudrate=args.baudrate,
        timeout=0.2,
        write_timeout=0.2,
    ) as serial_port:
        while time.monotonic() <= deadline:
            raw_line = serial_port.readline()
            if not raw_line:
                continue

            line = raw_line.decode("utf-8", errors="replace").rstrip("\r\n")
            captured_lines.append(line)
            if not args.quiet:
                print(line)

            update_marker_set(line, required_markers, seen_required)
            update_marker_set(line, DEFAULT_OPTIONAL_MARKERS, seen_optional)

            if required_markers and seen_required.issuperset(required_markers):
                return captured_lines, seen_required, seen_optional

    return captured_lines, seen_required, seen_optional


def format_missing_markers(markers: Sequence[str], seen_markers: set[str]) -> str:
    missing = [marker for marker in markers if marker not in seen_markers]
    if not missing:
        return ""
    return "\n".join(f"- {marker}" for marker in missing)


def main() -> int:
    args = parse_args()
    prefer_repo_python(repo_root())

    try:
        import serial  # type: ignore[import-not-found]
        from serial.tools import list_ports  # type: ignore[import-not-found]
    except ImportError:
        print(
            "pyserial is required for smoke testing. Install it in the repo-local "
            "environment with '.venv\\Scripts\\python.exe -m pip install pyserial'.",
            file=sys.stderr,
        )
        return 2

    visible_ports = [as_port_info(port) for port in list_ports.comports()]
    if args.list_ports:
        list_visible_ports(visible_ports)
        return 0

    required_markers = get_required_markers(args)

    try:
        port = wait_for_port(args, list_ports)
        print(f"Using serial port: {describe_port(port)}")
        captured_lines, seen_required, seen_optional = capture_console(
            args, serial, port, required_markers
        )
    except Exception as exc:
        print(exc, file=sys.stderr)
        return 1

    if required_markers and not seen_required.issuperset(required_markers):
        print(
            "Smoke test failed. Missing required console markers:\n"
            f"{format_missing_markers(required_markers, seen_required)}",
            file=sys.stderr,
        )
        if captured_lines:
            print("Captured console output:", file=sys.stderr)
            for line in captured_lines:
                print(line, file=sys.stderr)
        else:
            print(
                "No console output was captured before the timeout expired.",
                file=sys.stderr,
            )
        return 1

    print("Smoke test passed.")
    if seen_optional:
        print("Observed optional markers:")
        for marker in DEFAULT_OPTIONAL_MARKERS:
            if marker in seen_optional:
                print(f"- {marker}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
