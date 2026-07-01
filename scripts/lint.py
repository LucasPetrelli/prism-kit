#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["pyyaml"]
# ///

"""Run clang-tidy across the prism-kit source tree.

Usage::

    uv run lint                          # lint all prism-kit .cpp files
    uv run lint --fix                    # apply auto-fixes
    uv run lint app/src/hw/strip_manager.cpp  # single file

The script filters the Zephyr ``compile_commands.json`` to remove
cross-compiler flags that host clang-tidy does not recognise, and
rewrites the compiler path so clang-tidy can parse the translation
units.

On Windows::

    python scripts/lint.py
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

# ── Flags that host clang-tidy cannot handle ─────────────────────────────
STRIP_FLAGS = {
    # ARM cross-compiler target flags
    "-mthumb",
    "-mabi=aapcs",
    "-mfp16-format=ieee",
    "-mfpu=fpv4-sp-d16",
    "-mfloat-abi=hard",
    "-mcpu=cortex-m0plus",
    "-mtune=cortex-m0plus",
    "-march=armv6-m",
    # Linker / code-gen flags that only GCC understands
    "-fno-defer-pop",
    "-fno-reorder-functions",
    "-fno-strict-aliasing",
    # Zephyr-specific optimisation overrides
    "-specs=nano.specs",
    "-specs=nosys.specs",
    "-nostartfiles",
}

# Flags that contain any of these substrings are stripped.
STRIP_SUBSTRINGS = (
    "-mfp16-format=",
    "-fno-reorder-functions",
    "-fno-defer-pop",
)

STRIP_PREFIXES = (
    "-Wa,",
    "-Wp,",
    "--specs=",
    "-L",
)


def _filter_command(cmd: str, clang_tidy_extra: list[str]) -> str:
    """Strip cross-compiler flags from *cmd* and add *clang_tidy_extra*."""
    parts = cmd.split()
    kept: list[str] = []
    skip_next = False

    for i, part in enumerate(parts):
        if skip_next:
            skip_next = False
            continue

        # Rewrite the compiler driver.
        if part.endswith("arm-none-eabi-g++") or part.endswith("arm-none-eabi-gcc"):
            kept.append("clang++")
            continue

        # Skip flag + value pairs we don't want.
        if part in ("--sysroot",) and i + 1 < len(parts):
            skip_next = True
            continue

        # Strip known bad flags.
        if part in STRIP_FLAGS:
            continue
        if any(part.startswith(p) for p in STRIP_PREFIXES):
            continue
        if any(sub in part for sub in STRIP_SUBSTRINGS):
            continue
        # Strip -mno-* flags
        if part.startswith("-mno-"):
            continue

        kept.append(part)

    return " ".join(kept + clang_tidy_extra)


def _find_clang_tidy() -> str:
    """Locate clang-tidy on PATH or in common install locations."""
    exe = shutil.which("clang-tidy")
    if exe:
        return exe
    # Common LLVM install paths on Windows
    candidates = [
        r"C:\Program Files\LLVM\bin\clang-tidy.exe",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\bin\clang-tidy.exe",
    ]
    for c in candidates:
        if os.path.isfile(c):
            return c
    print(
        "error: clang-tidy not found.  Install LLVM (https://llvm.org).",
        file=sys.stderr,
    )
    sys.exit(1)


def _staged_files(repo_root: Path) -> list[Path]:
    """Return the list of staged .cpp/.hpp files from the git index."""
    result = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR"],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        print(
            "error: could not list staged files — is this a git repo?", file=sys.stderr
        )
        return []
    staged = []
    for line in result.stdout.splitlines():
        path = repo_root / line
        if path.suffix in (".cpp", ".hpp", ".h", ".c") and path.exists():
            staged.append(path.resolve())
    return staged


_SOURCE_DIRS = ("app", "bal", "oshal", "protocol", "src")


def _find_includers(header: Path, repo_root: Path) -> list[Path]:
    """Return .cpp files under project source dirs that #include *header*.

    Resolves ``#include "..."`` and ``#include <...>`` directives against
    the project root and ``include/`` directory to find translation units
    that pull in *header*.
    """
    includers: list[Path] = []
    include_roots = [
        repo_root,
        repo_root / "include",
    ]

    for dir_name in _SOURCE_DIRS:
        src_dir = repo_root / dir_name
        if not src_dir.is_dir():
            continue
        for cpp in src_dir.rglob("*.cpp"):
            try:
                content = cpp.read_text(encoding="utf-8")
            except (OSError, UnicodeDecodeError):
                continue
            for line in content.splitlines():
                stripped = line.strip()
                if not stripped.startswith("#include"):
                    continue
                m = re.search(r'#include\s*[<"]([^>"]+)[>"]', stripped)
                if not m:
                    continue
                include_path = m.group(1)
                for root in include_roots:
                    candidate = (root / include_path).resolve()
                    if candidate == header:
                        includers.append(cpp.resolve())
                        break
                else:
                    continue
                break  # matched in this file, move to next .cpp
    return includers


def main() -> None:
    parser = argparse.ArgumentParser(description="Run clang-tidy on prism-kit")
    parser.add_argument(
        "files",
        nargs="*",
        help="Source files to lint (default: all app/ bal/ oshal/ protocol/ src/)",
    )
    parser.add_argument(
        "--staged",
        action="store_true",
        help="Lint only staged C/C++ files from the git index.",
    )
    parser.add_argument("--fix", action="store_true", help="Apply auto-fixes")
    parser.add_argument(
        "--list-checks", action="store_true", help="List enabled checks and exit"
    )
    parser.add_argument(
        "--clang-tidy-extra",
        action="append",
        default=[],
        help="Extra arguments to pass to clang-tidy (can be repeated)",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Show per-file processing messages and all clang-tidy output",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    build_dir = repo_root / "build"
    compile_db = build_dir / "compile_commands.json"

    clang_tidy = _find_clang_tidy()

    # ── Resolve files to lint ──────────────────────────────────────────
    if args.staged:
        targets = _staged_files(repo_root)
        if not targets:
            print("No staged C/C++ files to lint.")
            sys.exit(0)
    elif args.files:
        targets = [Path(f).resolve() for f in args.files]
    else:
        # Default: all .cpp / .hpp under project-owned directories.
        sources = (
            repo_root / "app",
            repo_root / "bal",
            repo_root / "oshal",
            repo_root / "protocol",
            repo_root / "src",
        )
        targets = []
        for s in sources:
            if s.is_dir():
                targets.extend(s.rglob("*.cpp"))
                targets.extend(s.rglob("*.hpp"))
        # Only lint files that exist in the compile_commands index.
        with open(compile_db) as f:
            db: list[dict[str, Any]] = json.load(f)
        indexed = {Path(e["file"]).resolve() for e in db}
        targets = [t for t in targets if t in indexed]

    if not targets:
        print("No source files to lint.")
        sys.exit(0)

    # ── Build a filtered compilation database ──────────────────────────
    clang_tidy_extra = args.clang_tidy_extra
    with open(compile_db) as f:
        original_db: list[dict[str, Any]] = json.load(f)

    # Index the original DB by resolved file path.
    original_index: dict[Path, dict[str, Any]] = {}
    for entry in original_db:
        fp = Path(entry["file"]).resolve()
        original_index[fp] = entry

    # ── Resolve header-only staged targets ───────────────────────────
    if args.staged:
        resolved: list[Path] = []
        for t in targets:
            if t in original_index:
                resolved.append(t)
            elif t.suffix in (".hpp", ".h"):
                includers = _find_includers(t, repo_root)
                if includers:
                    resolved.extend(includers)
                else:
                    rel = t.relative_to(repo_root)
                    print(f"Skipping header not directly compiled: {rel}")
            # else: .cpp not in DB — silently dropped
        # Deduplicate while preserving order.
        seen: set[Path] = set()
        targets = []
        for t in resolved:
            if t not in seen:
                seen.add(t)
                targets.append(t)

    filtered_db: list[dict[str, Any]] = []
    for t in targets:
        if t not in original_index:
            continue
        entry = dict(original_index[t])
        entry["command"] = _filter_command(entry.get("command", ""), clang_tidy_extra)
        filtered_db.append(entry)

    if not filtered_db:
        print("No compilation entries found for the requested files — nothing to lint.")
        sys.exit(0)

    # Write the filtered database as compile_commands.json in a temp dir.
    tmp_dir = tempfile.mkdtemp(prefix="clang-tidy-", dir=str(build_dir))
    tmp_db = os.path.join(tmp_dir, "compile_commands.json")
    with open(tmp_db, "w") as f:
        json.dump(filtered_db, f)

    # ── Build clang-tidy command ───────────────────────────────────────
    src_list = [str(t) for t in targets]
    cmd = [clang_tidy, f"-p={tmp_dir}"]
    if args.fix:
        cmd.append("--fix")
    if args.list_checks:
        cmd.append("--list-checks")
    cmd.extend(src_list)

    # Point clang-tidy at the ARM GCC C++ headers so <array>, <cstdint>, etc.
    # resolve correctly, and set the target triple so the correct
    # architecture-specific bits/ headers are found.
    arm_gcc = shutil.which("arm-none-eabi-g++")
    if arm_gcc:
        gcc_root = Path(arm_gcc).resolve().parent.parent
        cxx_include = gcc_root / "arm-none-eabi" / "include" / "c++"
        if cxx_include.is_dir():
            versions = sorted(cxx_include.iterdir(), reverse=True)
            for v in versions:
                if v.is_dir():
                    version_dir = v  # e.g. arm-none-eabi/include/c++/14.2.1
                    cmd.insert(1, f"--extra-arg-before=-cxx-isystem{version_dir}")
                    # Architecture-specific headers (e.g. bits/c++config.h).
                    for triple_dir in [
                        version_dir / "arm-none-eabi",
                        version_dir / "arm-none-eabi" / "thumb" / "v6-m" / "nofp",
                    ]:
                        if triple_dir.is_dir():
                            cmd.insert(
                                1, f"--extra-arg-before=-cxx-isystem{triple_dir}"
                            )
                    break
        # Set the target triple to match the ARM cross-compiler.
        cmd.insert(1, "--extra-arg-before=--target=arm-none-eabi")
        # Narrow target to the specific MCU so CMSIS and Zephyr arch headers
        # resolve correctly (avoids "#error Unknown Arm architecture profile"
        # and undeclared __get_PRIMASK).
        cmd.insert(1, "--extra-arg-before=-mcpu=cortex-m0plus")

    # Suppress diagnostics about cross-compiler flags that clang does not
    # recognise (we already strip the worst offenders from the compilation
    # database, but some sneak through via the GCC toolchain spec files).
    cmd.insert(1, "--extra-arg-before=-Wno-unknown-attributes")
    cmd.insert(1, "--extra-arg-before=-Wno-unknown-warning-option")
    cmd.insert(1, "--extra-arg-before=-Qunused-arguments")

    try:
        if args.verbose:
            subprocess.run(cmd, check=True)
        else:
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                print(result.stdout, end="", file=sys.stderr)
                print(result.stderr, end="", file=sys.stderr)
                sys.exit(result.returncode)
    except subprocess.CalledProcessError:
        sys.exit(1)
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
