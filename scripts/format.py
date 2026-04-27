#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


DEFAULT_SOURCE_DIRS = (
    "app",
    "bal",
    "include",
    "oshal",
    "src",
)
SOURCE_EXTENSIONS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}
IGNORED_PARTS = {".venv", "build", "__cmake_systeminformation"}
WINDOWS_CLANG_FORMAT = Path(r"C:\Program Files\LLVM\bin\clang-format.exe")
SUBMODULE_SOURCE_DIRS = {"bal", "oshal"}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Format repo C and C++ sources with the repo clang-format policy."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        help="Optional files or directories to format relative to the repo root.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Fail if any file is not formatted instead of rewriting files.",
    )
    parser.add_argument(
        "--staged",
        action="store_true",
        help="Format only staged C and C++ files from the current git index.",
    )
    parser.add_argument(
        "--git-add",
        action="store_true",
        help="Re-stage rewritten files after formatting. Only valid with --staged.",
    )
    parser.add_argument(
        "--clang-format",
        dest="clang_format",
        help="Explicit path to the clang-format executable.",
    )
    return parser.parse_args()


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def git_root(start_path: Path) -> Path:
    completed = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        cwd=start_path,
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        raise SystemExit(
            "Git is required for --staged but the repo root could not be resolved."
        )

    return Path(completed.stdout.strip()).resolve()


def resolve_clang_format(explicit_path: str | None) -> Path:
    if explicit_path:
        candidate = Path(explicit_path)
        if candidate.is_file():
            return candidate
        raise SystemExit(f"clang-format was not found at '{candidate}'.")

    discovered = shutil.which("clang-format")
    if discovered:
        return Path(discovered)

    if WINDOWS_CLANG_FORMAT.is_file():
        return WINDOWS_CLANG_FORMAT

    raise SystemExit(
        "clang-format was not found on PATH. Install LLVM clang-format or pass --clang-format."
    )


def is_source_file(path: Path) -> bool:
    return path.is_file() and path.suffix.lower() in SOURCE_EXTENSIONS


def is_ignored(path: Path) -> bool:
    return any(part in IGNORED_PARTS for part in path.parts)


def missing_target_message(root: Path, target: Path) -> str:
    try:
        relative_target = target.relative_to(root)
    except ValueError:
        return f"Path '{target}' does not exist."

    if not relative_target.parts:
        return f"Path '{target}' does not exist."

    top_level = relative_target.parts[0]
    if top_level in SUBMODULE_SOURCE_DIRS and (root / ".gitmodules").is_file():
        return (
            f"Path '{relative_target.as_posix()}' does not exist. "
            f"The '{top_level}' submodule is missing or not initialized. "
            "Run 'git submodule update --init --recursive' from the repository root and retry."
        )

    return f"Path '{target}' does not exist."


def iter_source_files(root: Path, target: Path) -> list[Path]:
    if not target.exists():
        raise SystemExit(missing_target_message(root, target))

    if is_ignored(target):
        return []

    if is_source_file(target):
        return [target]

    if target.is_dir():
        files = [
            path
            for path in target.rglob("*")
            if is_source_file(path) and not is_ignored(path.relative_to(root))
        ]
        return sorted(files)

    return []


def collect_targets(root: Path, requested_paths: list[str]) -> list[Path]:
    targets = (
        [root / path for path in requested_paths]
        if requested_paths
        else [root / path for path in DEFAULT_SOURCE_DIRS]
    )
    files: set[Path] = set()

    for target in targets:
        files.update(iter_source_files(root, target.resolve()))

    return sorted(files)


def collect_staged_targets(root: Path) -> list[Path]:
    completed = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"],
        cwd=root,
        check=False,
        capture_output=True,
    )
    if completed.returncode != 0:
        raise SystemExit("Unable to read staged files from git.")

    entries = [entry for entry in completed.stdout.decode("utf-8").split("\0") if entry]
    files: set[Path] = set()
    for entry in entries:
        candidate = (root / entry).resolve()
        if not candidate.exists():
            continue
        if is_ignored(candidate.relative_to(root)):
            continue
        if is_source_file(candidate):
            files.add(candidate)

    return sorted(files)


def build_command(clang_format: Path, check: bool, file_path: Path) -> list[str]:
    command = [str(clang_format), "-style=file"]
    if check:
        command.extend(["--dry-run", "--Werror"])
    else:
        command.append("-i")
    command.append(str(file_path))
    return command


def run_formatter(clang_format: Path, files: list[Path], check: bool) -> int:
    if not files:
        print("No matching C or C++ source files found.")
        return 0

    had_failure = False
    for file_path in files:
        completed = subprocess.run(
            build_command(clang_format, check, file_path), check=False
        )
        if completed.returncode != 0:
            had_failure = True

    if check:
        if had_failure:
            print("clang-format check failed.", file=sys.stderr)
            return 1
        print(f"clang-format check passed for {len(files)} file(s).")
        return 0

    if had_failure:
        print("clang-format failed for at least one file.", file=sys.stderr)
        return 1

    print(f"Formatted {len(files)} file(s).")
    return 0


def restage_files(root: Path, files: list[Path]) -> int:
    if not files:
        return 0

    completed = subprocess.run(
        [
            "git",
            "add",
            "--",
            *[str(file_path.relative_to(root)) for file_path in files],
        ],
        cwd=root,
        check=False,
    )
    if completed.returncode != 0:
        print("Failed to re-stage formatted files.", file=sys.stderr)
        return 1

    print(f"Re-staged {len(files)} file(s).")
    return 0


def main() -> int:
    args = parse_args()
    if args.paths and args.staged:
        raise SystemExit("--staged cannot be combined with explicit paths.")

    if args.git_add and not args.staged:
        raise SystemExit("--git-add requires --staged.")

    if args.git_add and args.check:
        raise SystemExit("--git-add cannot be combined with --check.")

    root = repo_root().resolve()
    if args.staged:
        root = git_root(root)

    clang_format = resolve_clang_format(args.clang_format)
    files = (
        collect_staged_targets(root)
        if args.staged
        else collect_targets(root, args.paths)
    )
    formatter_status = run_formatter(clang_format, files, args.check)
    if formatter_status != 0 or args.check or not args.git_add:
        return formatter_status

    return restage_files(root, files)


if __name__ == "__main__":
    raise SystemExit(main())
