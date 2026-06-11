#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Build the prism-kit firmware via ``west build``.

Usage::

    uv run build
    uv run build --no-opt
    uv run build --debug-opt
"""

from __future__ import annotations

import ctypes
import argparse
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import subprocess
import sys

DEFAULT_TOOLCHAIN_ROOT = Path(
    r"C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1"
)
GCC_ONLY_PATTERNS = (
    r"\s+-mfp16-format=\S+",
    r"\s+-fno-reorder-functions",
    r"\s+--param=\S+",
    r"\s+-fno-defer-pop",
)
REQUIRED_SUBMODULES = ("bal", "oshal")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the firmware with the repo-local Zephyr toolchain setup."
    )
    optimization_group = parser.add_mutually_exclusive_group()
    optimization_group.add_argument(
        "--no-opt",
        action="store_true",
        help="Build with CONFIG_NO_OPTIMIZATIONS=y for instruction-by-instruction debugging.",
    )
    optimization_group.add_argument(
        "--debug-opt",
        action="store_true",
        help="Build with CONFIG_DEBUG_OPTIMIZATIONS=y for a more debuggable -Og style build.",
    )
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def _python_has_west(python_exe: Path) -> bool:
    """Return True if *python_exe* can import the ``west`` module."""
    try:
        result = subprocess.run(
            [str(python_exe), "-m", "west", "--version"],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=10,
        )
        return result.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def resolve_python_with_west(root: Path) -> Path:
    """Return a Python executable that has ``west`` available.

    Tries, in order:
    1. The current interpreter (covers ``uv run build`` with west in venv).
    2. The base interpreter outside the venv (covers system-wide ``west``).
    3. The parent Zephyr workspace venv (``<root>/../.venv/Scripts/python.exe``).
    """
    candidates: list[Path] = [
        Path(sys.executable).resolve(),
    ]

    # If we're inside a venv, the base interpreter may have west installed.
    base = Path(sys.base_exec_prefix).resolve()
    base_python = base / "python.exe" if os.name == "nt" else base / "bin" / "python3"
    if base_python != candidates[0] and base_python.is_file():
        candidates.append(base_python)

    candidates.append(root.parent / ".venv" / "Scripts" / "python.exe")

    for candidate in candidates:
        if candidate.is_file() and _python_has_west(candidate):
            resolved = candidate.resolve()
            if resolved == Path(sys.executable).resolve():
                return resolved
            # Re-execute with the candidate that has west.
            completed = subprocess.run(
                [str(resolved), __file__, *sys.argv[1:]], check=False
            )
            raise SystemExit(completed.returncode)

    raise SystemExit(
        "The 'west' module was not found in the current Python environment or "
        "in the parent workspace venv.\n"
        "  Install it with:  uv tool install west\n"
        "  Or run:           uv pip install west"
    )


def get_short_path(path: Path) -> Path:
    if os.name != "nt":
        return path

    get_short_path_name = ctypes.windll.kernel32.GetShortPathNameW
    get_short_path_name.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p, ctypes.c_uint]
    get_short_path_name.restype = ctypes.c_uint

    buffer_size = 260
    while True:
        buffer = ctypes.create_unicode_buffer(buffer_size)
        result = get_short_path_name(str(path), buffer, buffer_size)
        if result == 0:
            raise OSError(
                ctypes.get_last_error(), f"Failed to resolve short path for '{path}'."
            )
        if result < buffer_size:
            return Path(buffer.value)
        buffer_size = result + 1


def resolve_toolchain_root() -> Path:
    configured_root = os.environ.get("GNUARMEMB_TOOLCHAIN_PATH")
    candidate = Path(configured_root) if configured_root else DEFAULT_TOOLCHAIN_ROOT

    if not candidate.exists() and candidate == DEFAULT_TOOLCHAIN_ROOT:
        raise SystemExit(
            "GNU Arm Embedded Toolchain 14.2 rel1 was not found at the expected install path.\n"
            "Install it first, then rerun this script."
        )

    toolchain_root = get_short_path(candidate)
    compiler = toolchain_root / "bin" / "arm-none-eabi-gcc.exe"
    if not compiler.is_file():
        raise SystemExit(
            f"GNU Arm Embedded Toolchain was not found at '{toolchain_root}'.\n"
            "Set GNUARMEMB_TOOLCHAIN_PATH to a valid install root, then rerun this script."
        )

    return toolchain_root


def ensure_required_submodules(root: Path) -> None:
    missing_submodules = [
        name
        for name in REQUIRED_SUBMODULES
        if not (root / name / "CMakeLists.txt").is_file()
    ]
    if not missing_submodules:
        return

    missing_list = ", ".join(f"'{name}'" for name in missing_submodules)
    raise SystemExit(
        f"Required submodule(s) {missing_list} are missing or not initialized.\n"
        "Run 'git submodule update --init --recursive' from the repository root, then rerun this script."
    )


def remove_build_directory(root: Path) -> None:
    shutil.rmtree(root / "build", ignore_errors=True)


def run_west_build(
    root: Path,
    python_exe: Path,
    toolchain_root: Path,
    extra_conf_files: list[Path],
) -> None:
    env = os.environ.copy()
    env["ZEPHYR_TOOLCHAIN_VARIANT"] = "gnuarmemb"
    env["GNUARMEMB_TOOLCHAIN_PATH"] = str(toolchain_root)
    cmake_args = ["-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    if extra_conf_files:
        extra_conf_value = ";".join(str(path) for path in extra_conf_files)
        cmake_args.append(f"-DEXTRA_CONF_FILE={extra_conf_value}")

    subprocess.run(
        [
            str(python_exe),
            "-m",
            "west",
            "build",
            "-b",
            "seeeduino_xiao",
            "--pristine",
            "always",
            ".",
            "--",
            *cmake_args,
        ],
        cwd=root,
        env=env,
        check=True,
    )


def append_cpp_include_flags(command: str, compiler: str) -> str:
    compiler_path = PureWindowsPath(compiler)
    toolchain_root = Path(compiler_path.parent.parent)
    cpp_include_root = toolchain_root / "arm-none-eabi" / "include" / "c++"
    if not cpp_include_root.is_dir():
        return command

    cpp_versions = sorted(
        path.name for path in cpp_include_root.iterdir() if path.is_dir()
    )
    if not cpp_versions:
        return command

    cpp_version = cpp_versions[-1]
    cpp_include_flags = (
        f" -isystem {toolchain_root / 'arm-none-eabi' / 'include' / 'c++' / cpp_version}"
        f" -isystem {toolchain_root / 'arm-none-eabi' / 'include' / 'c++' / cpp_version / 'arm-none-eabi'}"
        f" -isystem {toolchain_root / 'arm-none-eabi' / 'include' / 'c++' / cpp_version / 'backward'}"
        f" -isystem {toolchain_root / 'lib' / 'gcc' / 'arm-none-eabi' / cpp_version / 'include'}"
        f" -isystem {toolchain_root / 'lib' / 'gcc' / 'arm-none-eabi' / cpp_version / 'include-fixed'}"
        f" -isystem {toolchain_root / 'arm-none-eabi' / 'include'}"
    )
    if compiler.endswith("g++.exe") and cpp_include_flags not in command:
        return command.replace(
            "--target=arm-none-eabi", f"--target=arm-none-eabi{cpp_include_flags}", 1
        )
    return command


def sanitize_compile_database(root: Path) -> None:
    build_db = root / "build" / "compile_commands.json"
    root_db = root / "compile_commands.json"
    entries = json.loads(build_db.read_text(encoding="utf-8"))
    sanitized_entries = []

    for entry in entries:
        command = entry.get("command")
        if command is None:
            raise RuntimeError(
                "Expected Zephyr compile_commands.json entries to contain 'command'."
            )

        compiler, _, remainder = command.partition(" ")
        if not remainder:
            raise RuntimeError("Expected compiler command followed by arguments.")

        for pattern in GCC_ONLY_PATTERNS:
            command = re.sub(pattern, "", command)

        command = command.replace(".exe ", ".exe --target=arm-none-eabi ", 1)
        command = append_cpp_include_flags(command, compiler)

        sanitized_entry = {
            "directory": entry["directory"],
            "file": entry["file"],
            "command": command,
        }
        if "output" in entry:
            sanitized_entry["output"] = entry["output"]
        sanitized_entries.append(sanitized_entry)

    root_db.write_text(json.dumps(sanitized_entries, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    args = parse_args()
    root = repo_root()
    python_exe = resolve_python_with_west(root)
    ensure_required_submodules(root)
    toolchain_root = resolve_toolchain_root()
    extra_conf_files: list[Path] = []
    if args.no_opt:
        extra_conf_files.append(root / "prj.noopt.conf")
    elif args.debug_opt:
        extra_conf_files.append(root / "prj.debug.conf")

    os.chdir(root)
    remove_build_directory(root)
    run_west_build(root, python_exe, toolchain_root, extra_conf_files)
    sanitize_compile_database(root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
