#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable


REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build" / "zephyr"
CONFIG_PATH = BUILD_DIR / ".config"
ARTIFACT_SUFFIXES = (".elf", ".hex", ".bin")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Flash the newest Zephyr artifact to the XIAO SAMD21 with SEGGER J-Link."
    )
    parser.add_argument(
        "artifact",
        nargs="?",
        help="Optional path to a specific .elf, .hex, or .bin artifact. Defaults to the newest supported file under build/zephyr.",
    )
    parser.add_argument("--device", default="ATSAMD21G18A", help="J-Link device name.")
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
    for candidate in candidate_jlink_paths():
        if candidate.exists():
            return candidate.resolve()

    raise FileNotFoundError(
        "J-Link Commander was not found. Install SEGGER J-Link and make JLink.exe available on PATH, "
        "or set JLINK_EXE to its full path."
    )


def resolve_artifact_path(user_supplied: str | None) -> Path:
    if user_supplied:
        artifact = Path(user_supplied).expanduser().resolve()
        if not artifact.exists():
            raise FileNotFoundError(f"Artifact '{artifact}' does not exist.")
        if artifact.suffix.lower() not in ARTIFACT_SUFFIXES:
            raise ValueError("Artifact must be one of: .elf, .hex, .bin")
        return artifact

    if not BUILD_DIR.exists():
        raise FileNotFoundError(
            f"Build directory '{BUILD_DIR}' was not found. Build the firmware first."
        )

    for suffix in ARTIFACT_SUFFIXES:
        canonical_candidate = BUILD_DIR / f"zephyr{suffix}"
        if canonical_candidate.is_file():
            return canonical_candidate

        candidates = [path for path in BUILD_DIR.glob(f"*{suffix}") if path.is_file()]
        if candidates:
            return max(candidates, key=lambda path: path.stat().st_mtime)

    raise FileNotFoundError(
        f"No supported artifacts were found under '{BUILD_DIR}'. Build the firmware first."
    )


def read_kconfig_hex(symbol_name: str) -> int:
    if not CONFIG_PATH.exists():
        raise FileNotFoundError(
            f"Zephyr config '{CONFIG_PATH}' was not found, so the .bin load address could not be determined."
        )

    expected_prefix = f"{symbol_name}="
    for line in CONFIG_PATH.read_text(encoding="utf-8").splitlines():
        if line.startswith(expected_prefix):
            return int(line.split("=", 1)[1], 16)

    raise ValueError(f"{symbol_name} was not found in '{CONFIG_PATH}'.")


def resolve_bin_load_address() -> int:
    flash_base = read_kconfig_hex("CONFIG_FLASH_BASE_ADDRESS")
    flash_offset = read_kconfig_hex("CONFIG_FLASH_LOAD_OFFSET")
    return flash_base + flash_offset


def build_load_command(artifact: Path) -> str:
    if artifact.suffix.lower() == ".bin":
        return f'loadfile "{artifact}" 0x{resolve_bin_load_address():X}'
    return f'loadfile "{artifact}"'


def run_flash(jlink_exe: Path, artifact: Path, args: argparse.Namespace) -> int:
    load_command = build_load_command(artifact)
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
        print(f"J-Link load command: {load_command}")

        if args.dry_run:
            print("Dry run requested; not invoking J-Link.")
            return 0

        completed = subprocess.run(command, check=False)
        return completed.returncode
    finally:
        commander_script.unlink(missing_ok=True)


def main() -> int:
    args = parse_args()
    try:
        jlink_exe = resolve_jlink_executable()
        artifact = resolve_artifact_path(args.artifact)
        return run_flash(jlink_exe, artifact, args)
    except Exception as exc:
        print(exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
