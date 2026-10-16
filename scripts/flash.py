#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Flash the latest Prism Kit Zephyr artifact with SEGGER J-Link.

Usage::

    uv run flash
    uv run flash path/to/zephyr.elf --dry-run
    uv run flash path/to/external.elf --device nRF52840_xxAA --dry-run
    uv run flash path/to/external.bin --device nRF52840_xxAA --load-address 0x0
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable

if __package__:
    from .build_provenance import (
        ARTIFACT_SUFFIXES,
        build_config_path,
        load_build_provenance,
    )
else:
    from build_provenance import (
        ARTIFACT_SUFFIXES,
        build_config_path,
        load_build_provenance,
    )

REPO_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = build_config_path(REPO_ROOT)
BOARD_TARGET_TO_JLINK_DEVICE = {
    "seeeduino_xiao/samd21g18a": "ATSAMD21G18A",
    "xiao_ble/nrf52840/sense": "nRF52840_xxAA",
}


def parse_load_address(value: str) -> int:
    """Parse a non-negative load address from decimal or base-prefixed text.

    Args:
        value: Address text supplied to ``--load-address``.

    Returns:
        The parsed address.

    Raises:
        argparse.ArgumentTypeError: If the text is invalid or negative.
    """
    try:
        address = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(
            "load address must be an integer such as 0x0 or 0"
        ) from exc
    if address < 0:
        raise argparse.ArgumentTypeError("load address must be non-negative")
    return address


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for the flash script.

    Returns:
        Parsed arguments with artifact path, device, interface, speed,
        and dry-run flags.
    """
    parser = argparse.ArgumentParser(
        description="Flash the latest Prism Kit Zephyr artifact with SEGGER J-Link."
    )
    parser.add_argument(
        "artifact",
        nargs="?",
        help=(
            "Optional explicit .elf, .hex, or .bin artifact. Without this, "
            "the latest successful build provenance is required."
        ),
    )
    parser.add_argument(
        "--device",
        default=None,
        help=(
            "Override the J-Link device for an explicit artifact. Without an "
            "artifact override, a valid build provenance record is still required."
        ),
    )
    parser.add_argument(
        "--load-address",
        type=parse_load_address,
        default=None,
        help=(
            "Explicit load address for an unverified .bin artifact, or an "
            "override for a provenance-verified project .bin."
        ),
    )
    parser.add_argument("--interface", default="SWD", help="Debug interface to use.")
    parser.add_argument(
        "--speed", default="4000", help="J-Link connection speed in kHz."
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the resolved tool, artifact, and J-Link command without flashing.",
    )
    return parser.parse_args()


def candidate_jlink_paths() -> Iterable[Path]:
    """Yield candidate paths to the J-Link Commander executable.

    Checks, in order: the ``JLINK_EXE`` environment variable, ``PATH``,
    and common SEGGER installation directories.
    """
    env_override = os.environ.get("JLINK_EXE")
    if env_override:
        yield Path(env_override)

    for executable_name in ("JLink.exe", "JLink"):
        resolved = shutil.which(executable_name)
        if resolved:
            yield Path(resolved)

    segger_roots = [
        Path(os.environ.get("ProgramFiles", "")) / "SEGGER",
        Path(os.environ.get("ProgramFiles(x86)", "")) / "SEGGER",
    ]
    for segger_root in segger_roots:
        if not str(segger_root) or not segger_root.exists():
            continue

        direct = segger_root / "JLink" / "JLink.exe"
        if direct.exists():
            yield direct

        for candidate in sorted(segger_root.glob("JLink*"), reverse=True):
            jlink_exe = candidate / "JLink.exe"
            if jlink_exe.exists():
                yield jlink_exe


def resolve_jlink_executable() -> Path:
    """Resolve the J-Link Commander executable path.

    Returns:
        The first existing candidate from :func:`candidate_jlink_paths`.

    Raises:
        FileNotFoundError: If no J-Link executable can be found.
    """
    for candidate in candidate_jlink_paths():
        if candidate.exists():
            return candidate.resolve()

    raise FileNotFoundError(
        "J-Link Commander was not found. Install SEGGER J-Link and make JLink.exe available on PATH, "
        "or set JLINK_EXE to its full path."
    )


def resolve_artifact_path(user_supplied: str) -> Path:
    """Resolve an explicitly supplied firmware artifact.

    Args:
        user_supplied: Explicit path to an ``.elf``, ``.hex``, or ``.bin`` file.

    Returns:
        The resolved artifact path.

    Raises:
        FileNotFoundError: If the artifact does not exist.
        ValueError: If the artifact has an unsupported file type.
    """
    artifact = Path(user_supplied).expanduser().resolve()
    if not artifact.is_file():
        raise FileNotFoundError(f"Artifact '{artifact}' does not exist.")
    if artifact.suffix.lower() not in ARTIFACT_SUFFIXES:
        raise ValueError("Artifact must be one of: .elf, .hex, .bin")
    return artifact


def jlink_device_for_board_target(board_target: str) -> str:
    """Map a supported Zephyr board target to its SEGGER device name.

    Args:
        board_target: Fully qualified value from ``CONFIG_BOARD_TARGET``.

    Returns:
        The matching J-Link device name.

    Raises:
        ValueError: If the board target has no supported J-Link mapping.
    """
    try:
        return BOARD_TARGET_TO_JLINK_DEVICE[board_target]
    except KeyError as exc:
        supported = ", ".join(BOARD_TARGET_TO_JLINK_DEVICE)
        raise ValueError(
            f"Unsupported CONFIG_BOARD_TARGET='{board_target}'. Supported targets: "
            f"{supported}. Build for a supported target before flashing."
        ) from exc


def resolve_jlink_device(
    device_override: str | None,
    board_target: str,
) -> str:
    """Resolve or override the J-Link device for a verified board target.

    Args:
        device_override: Explicit ``--device`` value, or ``None`` to use the
            board mapping.
        board_target: Verified fully qualified Zephyr board target.

    Returns:
        The explicit or metadata-derived J-Link device.

    Raises:
        ValueError: If the board target has no supported J-Link mapping.
    """
    mapped_device = jlink_device_for_board_target(board_target)
    return device_override if device_override else mapped_device


def resolve_flash_request(
    args: argparse.Namespace,
    root: Path | None = None,
) -> tuple[Path, str, Path]:
    """Resolve an artifact, device, and config from trusted provenance.

    The no-argument path requires a valid successful-build manifest. Explicit
    artifacts may use that manifest only when they are the exact recorded file;
    otherwise they are treated as manual external inputs.

    Args:
        args: Parsed flash CLI arguments.
        root: Prism Kit project root, or ``None`` to use this script's root.

    Returns:
        The artifact path, resolved J-Link device, and matching config path.

    Raises:
        FileNotFoundError: If required provenance or artifacts are missing.
        ValueError: If metadata is invalid or an explicit artifact lacks the
            required device or binary load-address overrides.
    """
    project_root = root if root is not None else REPO_ROOT
    if args.device is not None and not args.device.strip():
        raise ValueError("--device must not be empty.")

    config_path = build_config_path(project_root)
    if args.artifact is None:
        provenance = load_build_provenance(project_root)
        artifact = provenance.artifact_path
        if args.load_address is not None and artifact.suffix.lower() != ".bin":
            raise ValueError("--load-address is only valid for .bin artifacts.")
        device = resolve_jlink_device(args.device, provenance.board_target)
        return artifact, device, config_path

    artifact = resolve_artifact_path(args.artifact)
    if args.load_address is not None and artifact.suffix.lower() != ".bin":
        raise ValueError("--load-address is only valid for .bin artifacts.")

    provenance = None
    build_dir = (project_root / "build" / "zephyr").resolve()
    try:
        artifact.relative_to(build_dir)
    except ValueError:
        pass
    else:
        try:
            candidate_provenance = load_build_provenance(project_root)
        except (OSError, ValueError):
            pass
        else:
            if artifact == candidate_provenance.artifact_path:
                provenance = candidate_provenance

    if provenance is not None:
        device = resolve_jlink_device(args.device, provenance.board_target)
        return artifact, device, config_path

    if not args.device:
        raise ValueError(
            f"Explicit artifact '{artifact}' is not verified by the latest "
            "build provenance. Pass --device explicitly."
        )

    if artifact.suffix.lower() == ".bin" and args.load_address is None:
        raise ValueError(
            "An unverified .bin artifact requires an explicit --load-address; "
            "the project's config cannot safely supply its address."
        )

    return artifact, args.device, config_path


def read_kconfig_hex(symbol_name: str, config_path: Path = CONFIG_PATH) -> int:
    """Read a Kconfig hex value from the Zephyr ``.config`` file.

    Args:
        symbol_name: The Kconfig symbol to look up (e.g. ``CONFIG_FLASH_BASE_ADDRESS``).
        config_path: The Zephyr config file containing the symbol.

    Returns:
        The integer value of the symbol.

    Raises:
        FileNotFoundError: If the config file does not exist.
        ValueError: If the symbol is not found in ``.config``.
    """
    if not config_path.exists():
        raise FileNotFoundError(
            f"Zephyr config '{config_path}' was not found, so the .bin load address could not be determined."
        )

    expected_prefix = f"{symbol_name}="
    for line in config_path.read_text(encoding="utf-8").splitlines():
        if line.startswith(expected_prefix):
            return int(line.split("=", 1)[1], 16)

    raise ValueError(f"{symbol_name} was not found in '{config_path}'.")


def resolve_bin_load_address(config_path: Path = CONFIG_PATH) -> int:
    """Compute the flash load address from Kconfig.

    Adds ``CONFIG_FLASH_BASE_ADDRESS`` and ``CONFIG_FLASH_LOAD_OFFSET``.

    Returns:
        The combined flash load address.
    """
    flash_base = read_kconfig_hex("CONFIG_FLASH_BASE_ADDRESS", config_path)
    flash_offset = read_kconfig_hex("CONFIG_FLASH_LOAD_OFFSET", config_path)
    return flash_base + flash_offset


def build_load_command(
    artifact: Path,
    load_address: int | None = None,
    config_path: Path = CONFIG_PATH,
) -> str:
    """Build the J-Link Commander ``loadfile`` command for *artifact*.

    For ``.bin`` artifacts, includes the computed flash load address.

    Args:
        artifact: Path to the firmware artifact (``.elf``, ``.hex``, or ``.bin``).
        load_address: Explicit .bin load address, or ``None`` to use the
            matching project's config.
        config_path: Zephyr config used when resolving an implicit .bin address.

    Returns:
        The J-Link load command string.
    """
    if artifact.suffix.lower() == ".bin":
        address = (
            load_address
            if load_address is not None
            else resolve_bin_load_address(config_path)
        )
        if address < 0:
            raise ValueError("The .bin load address must be non-negative.")
        return f'loadfile "{artifact}" 0x{address:X}'
    return f'loadfile "{artifact}"'


def run_flash(
    jlink_exe: Path,
    artifact: Path,
    args: argparse.Namespace,
    config_path: Path = CONFIG_PATH,
) -> int:
    """Flash *artifact* to the target using J-Link Commander.

    Builds a temporary J-Link commander script, invokes the tool, and
    cleans up the script afterwards.

    Args:
        jlink_exe: Path to the J-Link Commander executable.
        artifact: Path to the firmware artifact.
        args: Parsed command-line arguments (device, interface, speed, dry_run).
        config_path: The config fingerprinted by the successful-build manifest.

    Returns:
        The J-Link process exit code.
    """
    load_command = build_load_command(artifact, args.load_address, config_path)
    commander_lines = [
        "r",
        "h",
        load_command,
        "r",
        "g",
        "qc",
    ]

    with tempfile.NamedTemporaryFile(
        "w", suffix=".jlink", delete=False, encoding="ascii"
    ) as handle:
        handle.write("\n".join(commander_lines) + "\n")
        commander_script = Path(handle.name)

    command = [
        str(jlink_exe),
        "-NoGui",
        "1",
        "-ExitOnError",
        "1",
        "-device",
        args.device,
        "-if",
        args.interface,
        "-speed",
        str(args.speed),
        "-autoconnect",
        "1",
        "-CommanderScript",
        str(commander_script),
    ]

    try:
        print(f"Using J-Link: {jlink_exe}")
        print(f"Using artifact: {artifact}")
        print(f"Using J-Link device: {args.device}")
        print(f"J-Link load command: {load_command}")

        if args.dry_run:
            print("Dry run requested; not invoking J-Link.")
            return 0

        completed = subprocess.run(command, check=False)
        return completed.returncode
    finally:
        commander_script.unlink(missing_ok=True)


def main() -> int:
    """Run the flash pipeline.

    Returns:
        0 on success, 1 on failure.
    """
    args = parse_args()
    try:
        artifact, args.device, config_path = resolve_flash_request(args)
        jlink_exe = resolve_jlink_executable()
        return run_flash(jlink_exe, artifact, args, config_path)
    except Exception as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
