#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["ruff"]
# ///

"""Run ruff docstring and style checks on project Python sources.

Usage::

    uv run lint-python                        # check all scripts/
    uv run lint-python --fix                  # auto-fix where possible
    uv run lint-python --staged               # check only staged .py files
    uv run lint-python scripts/smoke_test.py  # single file

Wired into the pre-commit hook as the Python lint stage.  Uses the
ruff configuration from ``pyproject.toml``.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

DEFAULT_TARGETS = ["scripts"]
PYTHON_EXTENSIONS = {".py"}


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments for the Python lint script.

    Returns:
        Parsed arguments with ``--fix``, ``--staged``, and explicit
        path support.
    """
    parser = argparse.ArgumentParser(
        description="Run ruff docstring and style checks on project Python sources."
    )
    parser.add_argument(
        "paths",
        nargs="*",
        help="Optional files or directories to check relative to the repo root.",
    )
    parser.add_argument(
        "--fix",
        action="store_true",
        help="Apply auto-fixes where possible.",
    )
    parser.add_argument(
        "--staged",
        action="store_true",
        help="Check only staged .py files from the current git index.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        default=True,
        help="Check mode (default). Exit non-zero on any violations.",
    )
    return parser.parse_args()


def repo_root() -> Path:
    """Return the repository root directory.

    Resolves relative to this script's location (``scripts/`` sibling).
    """
    return Path(__file__).resolve().parent.parent


def staged_python_files(root: Path) -> list[Path]:
    """Return the list of staged ``.py`` files from the git index.

    Args:
        root: The repository root directory.

    Returns:
        A sorted list of absolute paths to staged Python files.
    """
    result = subprocess.run(
        ["git", "diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        print(
            "error: could not list staged files — is this a git repo?",
            file=sys.stderr,
        )
        return []

    entries = [entry for entry in result.stdout.split("\0") if entry]
    files: list[Path] = []
    for entry in entries:
        candidate = (root / entry).resolve()
        if not candidate.exists():
            continue
        if candidate.suffix.lower() in PYTHON_EXTENSIONS:
            files.append(candidate)
    return sorted(files)


def resolve_targets(
    root: Path,
    requested_paths: list[str],
    staged: bool,
) -> list[Path]:
    """Resolve the list of files or directories to check.

    Args:
        root: The repository root directory.
        requested_paths: User-supplied paths (relative to root).
        staged: If True, return only staged Python files.

    Returns:
        A list of paths to pass to ruff.
    """
    if staged:
        return staged_python_files(root)

    if not requested_paths:
        return [root / t for t in DEFAULT_TARGETS]

    return [root / p for p in requested_paths]


def run_ruff(targets: list[Path], fix: bool) -> int:
    """Run ``ruff check`` on *targets*.

    Args:
        targets: Files or directories to check.
        fix: If True, pass ``--fix`` to ruff.

    Returns:
        The ruff process exit code.
    """
    cmd = ["ruff", "check"]
    if fix:
        cmd.append("--fix")
    cmd.extend(str(t) for t in targets)

    completed = subprocess.run(cmd, check=False)
    return completed.returncode


def main() -> int:
    """Run the Python lint pipeline.

    Returns:
        0 on success, non-zero if ruff finds violations.
    """
    args = parse_args()
    root = repo_root()

    targets = resolve_targets(root, args.paths, args.staged)

    if not targets:
        print("No Python files to check.")
        return 0

    # Only show the "Checking" banner when targets are explicit or
    # non-default to keep the pre-commit output concise.
    if args.paths or not args.staged:
        print(f"ruff check on {len(targets)} target(s)...")

    return run_ruff(targets, args.fix)


if __name__ == "__main__":
    raise SystemExit(main())
