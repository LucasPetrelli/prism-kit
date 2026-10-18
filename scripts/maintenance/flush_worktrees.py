#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""Remove all linked Git worktrees registered for this project.

Usage::

    uv run flush-worktrees
    uv run flush-worktrees --force

The worktree containing this script is kept. Without ``--force``, Git refuses
to remove worktrees with local changes. ``--force`` is passed to each
``git worktree remove`` command.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def resolve_project_root() -> Path:
    """Return the project root containing this script."""
    return Path(__file__).resolve().parents[2]


def parse_worktree_paths(output: str) -> list[Path]:
    """Parse worktree paths from ``git worktree list --porcelain -z`` output."""
    return [
        Path(field[len("worktree ") :])
        for field in output.split("\0")
        if field.startswith("worktree ")
    ]


def _run_git_capture(root: Path, args: list[str]) -> str | None:
    """Run Git from *root* and return stdout, or report a command error.

    Args:
        root: The project worktree used as the command's working directory.
        args: Arguments passed after ``git``.

    Returns:
        Captured standard output, or ``None`` if Git could not complete.
    """
    command = ["git", *args]
    try:
        result = subprocess.run(
            command,
            cwd=root,
            capture_output=True,
            text=True,
            check=True,
        )
    except FileNotFoundError:
        print("error: Git was not found on PATH.", file=sys.stderr)
        return None
    except subprocess.CalledProcessError as exc:
        print(f"error: {' '.join(command)} failed.", file=sys.stderr)
        detail = (exc.stderr or "").strip()
        if detail:
            print(detail, file=sys.stderr)
        return None
    except OSError as exc:
        print(f"error: could not run Git: {exc}", file=sys.stderr)
        return None

    return result.stdout


def parse_args() -> argparse.Namespace:
    """Parse command-line options."""
    parser = argparse.ArgumentParser(
        description="Remove every linked worktree except the one running this command."
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="pass --force to git worktree remove for each linked worktree",
    )
    return parser.parse_args()


def main() -> int:
    """Remove linked worktrees, returning non-zero if any removal fails."""
    args = parse_args()
    root = resolve_project_root()

    current_output = _run_git_capture(root, ["rev-parse", "--show-toplevel"])
    if current_output is None:
        return 1
    current_worktree = Path(current_output.rstrip("\r\n")).resolve()

    listing = _run_git_capture(root, ["worktree", "list", "--porcelain", "-z"])
    if listing is None:
        return 1

    worktrees = parse_worktree_paths(listing)
    resolved_worktrees = [(path, path.resolve()) for path in worktrees]
    if not any(resolved == current_worktree for _, resolved in resolved_worktrees):
        print(
            "error: could not identify the active worktree; refusing to remove any.",
            file=sys.stderr,
        )
        return 1

    linked_worktrees = [
        path for path, resolved in resolved_worktrees if resolved != current_worktree
    ]
    if not linked_worktrees:
        print("No linked worktrees to remove.")
        return 0

    failed = 0
    removed = 0
    for worktree in linked_worktrees:
        command = ["git", "worktree", "remove"]
        if args.force:
            command.append("--force")
        command.append(str(worktree))

        print(f"Removing linked worktree: {worktree}")
        try:
            result = subprocess.run(command, cwd=root, check=False)
        except OSError as exc:
            print(f"error: could not run Git for {worktree}: {exc}", file=sys.stderr)
            failed += 1
            continue

        if result.returncode != 0:
            print(
                f"error: Git could not remove {worktree} "
                f"(exit code {result.returncode}).",
                file=sys.stderr,
            )
            failed += 1
            continue
        removed += 1

    if failed:
        print(
            f"Removed {removed} linked worktree(s); {failed} removal(s) failed.",
            file=sys.stderr,
        )
        return 1

    print(f"Removed {removed} linked worktree(s).")
    return 0


def cli() -> int:
    """Run the flush-worktrees command with stdout and stderr logging."""
    if not __package__:
        sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from scripts.common.cli_logging import run_cli

    return run_cli("flush-worktrees", main)


if __name__ == "__main__":
    raise SystemExit(cli())
