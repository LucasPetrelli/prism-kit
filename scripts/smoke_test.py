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
DEFAULT_PORT_MATCH_TOKENS = ("Prism Kit",)
DEFAULT_DEBUG_MARKER = "DebugPort online on"
DEFAULT_COMMAND_MARKER = "CommandPort online on"
DEFAULT_OPTIONAL_MARKERS = ("Booting Zephyr OS build",)
DEFAULT_LOOPBACK_PAYLOAD = bytes.fromhex("01020304")


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
    return [str(marker) for marker in args.require]


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


def resolve_auto_ports(
    port_infos: Sequence[PortInfo], tokens: Sequence[str]
) -> list[PortInfo]:
    matches = [port for port in port_infos if port_matches_tokens(port, tokens)]
    if not matches:
        return [port for port in port_infos if looks_like_usb_serial_port(port)]

    return sorted(matches, key=lambda port: score_port(port, tokens), reverse=True)


def unique_ports(ports: Iterable[PortInfo]) -> list[PortInfo]:
    unique: list[PortInfo] = []
    seen_devices: set[str] = set()
    for port in ports:
        if port.device in seen_devices:
            continue
        seen_devices.add(port.device)
        unique.append(port)
    return unique


def wait_for_ports(args: argparse.Namespace, list_ports_module: Any) -> list[PortInfo]:
    deadline = time.monotonic() + args.wait_for_port
    tokens = get_port_match_tokens(args)
    last_snapshot: list[PortInfo] = []

    while time.monotonic() <= deadline:
        port_infos = [as_port_info(port) for port in list_ports_module.comports()]
        last_snapshot = port_infos

        explicit_ports: list[PortInfo] = []
        if args.port:
            resolved_debug = resolve_explicit_port(port_infos, args.port)
            if resolved_debug is None:
                time.sleep(0.25)
                continue
            explicit_ports.append(resolved_debug)

        if args.command_port:
            resolved_command = resolve_explicit_port(port_infos, args.command_port)
            if resolved_command is None:
                time.sleep(0.25)
                continue
            explicit_ports.append(resolved_command)

        auto_ports = resolve_auto_ports(port_infos, tokens)
        candidates = unique_ports([*explicit_ports, *auto_ports])
        if candidates:
            return candidates

        time.sleep(0.25)

    visible = (
        "\n".join(f"- {describe_port(port)}" for port in last_snapshot) or "- none"
    )
    if args.port or args.command_port:
        requested_ports = [port for port in (args.port, args.command_port) if port]
        raise RuntimeError(
            "Explicit serial ports did not appear within "
            f"{args.wait_for_port:.1f} seconds.\n"
            f"Requested ports: {', '.join(requested_ports)}\n"
            f"Visible ports at timeout:\n{visible}"
        )

    token_text = ", ".join(get_port_match_tokens(args)) or "<any port>"
    raise RuntimeError(
        "No serial ports matched the smoke-test discovery tokens within "
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
    ports: Sequence[PortInfo],
    required_markers: Sequence[str],
) -> tuple[dict[str, list[str]], set[str], set[str], dict[str, str]]:
    captured_lines: dict[str, list[str]] = {port.device: [] for port in ports}
    seen_required: set[str] = set()
    seen_optional: set[str] = set()
    role_ports: dict[str, str] = {}
    deadline = time.monotonic() + args.capture_timeout

    serial_ports = {
        port.device: serial_module.Serial(
            port.device,
            baudrate=args.baudrate,
            timeout=0.05,
            write_timeout=0.2,
        )
        for port in ports
    }

    try:
        while time.monotonic() <= deadline:
            for port in ports:
                raw_line = serial_ports[port.device].readline()
                if not raw_line:
                    continue

                line = raw_line.decode("utf-8", errors="replace").rstrip("\r\n")
                captured_lines[port.device].append(line)
                if not args.quiet:
                    print(f"[{port.device}] {line}")

                update_marker_set(line, required_markers, seen_required)
                update_marker_set(line, DEFAULT_OPTIONAL_MARKERS, seen_optional)

                if DEFAULT_DEBUG_MARKER in line:
                    role_ports["debug"] = port.device
                if DEFAULT_COMMAND_MARKER in line:
                    role_ports["command"] = port.device

            if args.no_default_requirements:
                if not required_markers or seen_required.issuperset(required_markers):
                    return captured_lines, seen_required, seen_optional, role_ports
                continue

            if (
                "debug" in role_ports
                and "command" in role_ports
                and role_ports["debug"] != role_ports["command"]
                and seen_required.issuperset(required_markers)
            ):
                return captured_lines, seen_required, seen_optional, role_ports
    finally:
        for serial_port in serial_ports.values():
            serial_port.close()

    return captured_lines, seen_required, seen_optional, role_ports


def format_missing_markers(markers: Sequence[str], seen_markers: set[str]) -> str:
    missing = [marker for marker in markers if marker not in seen_markers]
    if not missing:
        return ""
    return "\n".join(f"- {marker}" for marker in missing)


def print_captured_lines(captured_lines: dict[str, list[str]], stream: Any) -> None:
    for device, lines in captured_lines.items():
        for line in lines:
            print(f"[{device}] {line}", file=stream)


# ── Protocol wire-format helpers (for loopback testing) ──────────────────

SYNC_BYTE = 0xAA
TAG_LOOPBACK = 0x0000


def build_loopback_frame(payload: bytes) -> bytes:
    """Build a complete protocol wire frame for the loopback tag.

    Wire format: sync(1) | tag(2 LE) | length(2 LE) | data(N) | checksum(1)
    Checksum is XOR of tag + length + data bytes.
    """
    length = len(payload)
    header = TAG_LOOPBACK.to_bytes(2, "little") + length.to_bytes(2, "little")

    cs = 0
    for b in header:
        cs ^= b
    for b in payload:
        cs ^= b

    return bytes([SYNC_BYTE]) + header + payload + bytes([cs])


def parse_wire_frame(data: bytes) -> dict | None:
    """Try to parse a single protocol wire frame from *data*.

    Returns a dict with keys 'tag', 'payload', 'checksum_ok' on success,
    or None if the frame is too short or the sync byte is wrong.
    """
    if len(data) < 8:
        return None
    if data[0] != SYNC_BYTE:
        return None

    tag = int.from_bytes(data[1:3], "little")
    length = int.from_bytes(data[3:5], "little")

    frame_size = 5 + length + 1  # sync + header + data + checksum
    if len(data) < frame_size:
        return None

    payload = data[5 : 5 + length]
    received_cs = data[5 + length]

    # Compute expected checksum
    expected_cs = 0
    for b in data[1 : 5 + length]:
        expected_cs ^= b

    return {
        "tag": tag,
        "payload": payload,
        "checksum_ok": received_cs == expected_cs,
    }


def run_loopback_test(
    serial_module: Any,
    command_device: str,
    baudrate: int,
    payload: bytes,
    timeout: float = 5.0,
) -> bool:
    """Send a loopback frame and verify the echo response."""
    frame = build_loopback_frame(payload)

    with serial_module.Serial(
        command_device, baudrate=baudrate, timeout=0.1, write_timeout=1.0
    ) as port:
        # Flush any stale data.
        port.reset_input_buffer()
        port.reset_output_buffer()

        # Send the loopback frame.
        port.write(frame)

        # Read back bytes until we accumulate enough for a complete frame
        # plus echo, or until the timeout expires.
        deadline = time.monotonic() + timeout
        accumulated = bytearray()
        result: dict | None = None

        while time.monotonic() < deadline:
            chunk = port.read(64)
            if chunk:
                accumulated.extend(chunk)

            result = parse_wire_frame(bytes(accumulated))
            if result is not None:
                break

        if result is None:
            print(
                f"Loopback FAIL — no valid frame received within {timeout:.1f}s.",
                file=sys.stderr,
            )
            if accumulated:
                print(
                    f"  Raw bytes received: {accumulated.hex()}",
                    file=sys.stderr,
                )
            return False

        if result["tag"] != TAG_LOOPBACK:
            print(
                f"Loopback FAIL — unexpected tag 0x{result['tag']:04X} "
                f"(expected 0x{TAG_LOOPBACK:04X}).",
                file=sys.stderr,
            )
            return False

        if not result["checksum_ok"]:
            print(
                "Loopback FAIL — checksum mismatch on received frame.", file=sys.stderr
            )
            return False

        if result["payload"] != payload:
            print(
                f"Loopback FAIL — payload mismatch. "
                f"Sent {payload.hex()}, received {result['payload'].hex()}.",
                file=sys.stderr,
            )
            return False

    print("Loopback OK")
    return True


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
        ports = wait_for_ports(args, list_ports)
        print("Using serial ports:")
        for port in ports:
            print(f"- {describe_port(port)}")
        captured_lines, seen_required, seen_optional, role_ports = capture_console(
            args, serial, ports, required_markers
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
        if any(captured_lines.values()):
            print("Captured console output:", file=sys.stderr)
            print_captured_lines(captured_lines, sys.stderr)
        else:
            print(
                "No console output was captured before the timeout expired.",
                file=sys.stderr,
            )
        return 1

    if not args.no_default_requirements:
        missing_roles: list[str] = []
        if "debug" not in role_ports:
            missing_roles.append("debug")
        if "command" not in role_ports:
            missing_roles.append("command")
        if missing_roles:
            print(
                "Smoke test failed. Missing required port role markers:\n"
                + "\n".join(f"- {role}" for role in missing_roles),
                file=sys.stderr,
            )
            if any(captured_lines.values()):
                print("Captured console output:", file=sys.stderr)
                print_captured_lines(captured_lines, sys.stderr)
            return 1
        if "command" in role_ports and role_ports["debug"] == role_ports["command"]:
            print(
                "Smoke test failed. Debug and command markers were observed on "
                "the same serial port.",
                file=sys.stderr,
            )
            print_captured_lines(captured_lines, sys.stderr)
            return 1

    print("Smoke test passed.")
    if not args.no_default_requirements:
        print(f"Debug port: {role_ports['debug']}")
        if "command" in role_ports:
            print(f"Command port: {role_ports['command']}")
    if seen_optional:
        print("Observed optional markers:")
        for marker in DEFAULT_OPTIONAL_MARKERS:
            if marker in seen_optional:
                print(f"- {marker}")

    command_device = role_ports.get("command")
    if not command_device:
        command_device = next(
            (
                p.device
                for p in ports
                if p.serial_number and p.device != role_ports.get("debug")
            ),
            None,
        )

    if not command_device:
        print(
            "Smoke test failed — no command port identified for loopback test.",
            file=sys.stderr,
        )
        if args.no_default_requirements:
            print(
                "Note: use --command-port to specify the command port explicitly.",
                file=sys.stderr,
            )
        return 1

    print(
        f"Sending loopback ({DEFAULT_LOOPBACK_PAYLOAD.hex()}) on "
        f"{command_device} ..."
    )
    if not run_loopback_test(
        serial, command_device, args.baudrate, DEFAULT_LOOPBACK_PAYLOAD
    ):
        print("Smoke test failed — loopback test did not succeed.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
