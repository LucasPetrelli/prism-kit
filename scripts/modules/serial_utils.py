"""Serial port discovery, matching, and console capture utilities.

Project-agnostic — all project-specific markers and tokens are passed as
parameters so this module can be reused across different firmware projects.
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Any, Iterable, Sequence


@dataclass(frozen=True)
class PortInfo:
    device: str
    description: str
    product: str | None
    manufacturer: str | None
    serial_number: str | None
    hwid: str


# ── Port field helpers ──────────────────────────────────────────────────


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


def describe_port(port: PortInfo) -> str:
    """Human-readable description of a serial port."""
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


# ── Port matching / scoring ────────────────────────────────────────────


def port_matches_tokens(port: PortInfo, tokens: Sequence[str]) -> bool:
    """Return True if any *tokens* appears in any field of *port*."""
    if not tokens:
        return True

    haystack = "\n".join(field.lower() for field in port_fields(port))
    return any(token.lower() in haystack for token in tokens)


def looks_like_usb_serial_port(port: PortInfo) -> bool:
    """Heuristic: return True if the port looks like a USB serial device."""
    haystack = "\n".join(field.lower() for field in port_fields(port))
    # "serial" alone is too broad — it matches "Standard Serial over
    # Bluetooth link".  Require "usb", a VID:PID hardware ID, or a
    # non-empty USB serial number string.
    return "usb" in haystack or "vid:pid" in haystack or bool(port.serial_number)


def score_port(port: PortInfo, tokens: Sequence[str]) -> int:
    """Score *port* by how many *tokens* match its fields (exact > substring)."""
    score = 0
    haystack = [field.lower() for field in port_fields(port)]
    for token in tokens:
        normalized = token.lower()
        if any(normalized == field for field in haystack if field):
            score += 10
        elif any(normalized in field for field in haystack):
            score += 1
    return score


# ── Port resolution ────────────────────────────────────────────────────


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


# ── Port discovery ─────────────────────────────────────────────────────


def discover_ports(
    list_ports_module: Any,
    *,
    port: str | None = None,
    command_port: str | None = None,
    wait_for_port: float = 10.0,
    port_match_tokens: Sequence[str] = (),
) -> list[PortInfo]:
    """Poll ``list_ports_module.comports()`` until matching ports appear.

    Raises ``RuntimeError`` if no ports appear within *wait_for_port* seconds.
    """
    deadline = time.monotonic() + wait_for_port
    tokens = list(port_match_tokens)
    last_snapshot: list[PortInfo] = []

    while time.monotonic() <= deadline:
        port_infos = [as_port_info(p) for p in list_ports_module.comports()]
        last_snapshot = port_infos

        explicit_ports: list[PortInfo] = []
        if port:
            resolved = resolve_explicit_port(port_infos, port)
            if resolved is None:
                time.sleep(0.25)
                continue
            explicit_ports.append(resolved)

        if command_port:
            resolved = resolve_explicit_port(port_infos, command_port)
            if resolved is None:
                time.sleep(0.25)
                continue
            explicit_ports.append(resolved)

        auto = resolve_auto_ports(port_infos, tokens)
        candidates = unique_ports([*explicit_ports, *auto])
        if candidates:
            return candidates

        time.sleep(0.25)

    visible = "\n".join(f"- {describe_port(p)}" for p in last_snapshot) or "- none"
    if port or command_port:
        requested = [p for p in (port, command_port) if p]
        raise RuntimeError(
            "Explicit serial ports did not appear within "
            f"{wait_for_port:.1f} seconds.\n"
            f"Requested ports: {', '.join(requested)}\n"
            f"Visible ports at timeout:\n{visible}"
        )

    token_text = ", ".join(tokens) or "<any port>"
    raise RuntimeError(
        "No serial ports matched the smoke-test discovery tokens within "
        f"{wait_for_port:.1f} seconds.\n"
        f"Port match tokens: {token_text}\n"
        f"Visible ports at timeout:\n{visible}"
    )


# ── Marker helpers ─────────────────────────────────────────────────────


def update_marker_set(
    line: str, markers: Iterable[str], seen_markers: set[str]
) -> None:
    for marker in markers:
        if marker in line:
            seen_markers.add(marker)


def format_missing_markers(markers: Sequence[str], seen_markers: set[str]) -> str:
    missing = [marker for marker in markers if marker not in seen_markers]
    if not missing:
        return ""
    return "\n".join(f"- {marker}" for marker in missing)


def print_captured_lines(captured_lines: dict[str, list[str]], stream: Any) -> None:
    for device, lines in captured_lines.items():
        for line in lines:
            print(f"[{device}] {line}", file=stream)


# ── Console capture ────────────────────────────────────────────────────


def _should_stop_capturing(
    *,
    seen_required: set[str],
    required_markers: Sequence[str],
    check_roles: bool,
    role_ports: dict[str, str],
) -> bool:
    """Return True when the capture loop can stop early.

    Early exit requires:
    1. All required markers have been seen (if any are configured).
    2. If *check_roles* is True, both ``"debug"`` and ``"command"`` roles
       must be assigned to *different* ports.
    """
    if required_markers and not seen_required.issuperset(required_markers):
        return False

    if check_roles:
        return (
            "debug" in role_ports
            and "command" in role_ports
            and role_ports["debug"] != role_ports["command"]
        )

    return True


def capture_console(
    serial_module: Any,
    ports: Sequence[PortInfo],
    *,
    baudrate: int = 115200,
    capture_timeout: float = 12.0,
    required_markers: Sequence[str] = (),
    optional_markers: Sequence[str] = (),
    debug_marker: str = "",
    command_marker: str = "",
    quiet: bool = False,
) -> tuple[dict[str, list[str]], set[str], set[str], dict[str, str]]:
    """Open serial ports and capture console output.

    Reads lines from every port until the capture timeout or until
    ``_should_stop_capturing()`` returns True.

    Returns ``(captured_lines, seen_required, seen_optional, role_ports)``.

    *role_ports* maps ``"debug"`` / ``"command"`` to the device path where
    *debug_marker* / *command_marker* was first seen. If a marker is empty,
    the corresponding role is never set.
    """
    captured_lines: dict[str, list[str]] = {port.device: [] for port in ports}
    seen_required: set[str] = set()
    seen_optional: set[str] = set()
    role_ports: dict[str, str] = {}
    deadline = time.monotonic() + capture_timeout
    check_roles = bool(debug_marker) and bool(command_marker)

    serial_ports: dict[str, Any] = {}
    for port in ports:
        try:
            serial_ports[port.device] = serial_module.Serial(
                port.device,
                baudrate=baudrate,
                timeout=0.05,
                write_timeout=0.2,
            )
        except Exception as exc:
            print(
                f"Warning: skipping port {port.device} — {exc}",
                flush=True,
            )

    if not serial_ports:
        print(
            "Error: none of the discovered serial ports could be opened.",
            flush=True,
        )
        return captured_lines, seen_required, seen_optional, role_ports

    try:
        while time.monotonic() <= deadline:
            for device, serial_port in serial_ports.items():
                raw_line = serial_port.readline()
                if not raw_line:
                    continue

                line = raw_line.decode("utf-8", errors="replace").rstrip("\r\n")
                captured_lines[device].append(line)
                if not quiet:
                    print(f"[{device}] {line}")

                update_marker_set(line, required_markers, seen_required)
                update_marker_set(line, optional_markers, seen_optional)

                if debug_marker and debug_marker in line:
                    role_ports["debug"] = device
                if command_marker and command_marker in line:
                    role_ports["command"] = device

            if _should_stop_capturing(
                seen_required=seen_required,
                required_markers=required_markers,
                check_roles=check_roles,
                role_ports=role_ports,
            ):
                return captured_lines, seen_required, seen_optional, role_ports
    finally:
        for serial_port in serial_ports.values():
            serial_port.close()

    return captured_lines, seen_required, seen_optional, role_ports
