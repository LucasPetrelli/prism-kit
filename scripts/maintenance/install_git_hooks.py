#!/usr/bin/env python3
"""Install repo-local git hooks from the ``.githooks/`` directory.

Configures ``core.hooksPath`` to point at the repo-local hook scripts
and ensures the pre-commit hook is executable on POSIX platforms.

Usage::

    uv run install-git-hooks
"""

from __future__ import annotations

import os
import stat
import subprocess
import sys
from pathlib import Path

SUBMODULES = ("bal", "oshal")


def repo_root() -> Path:
    """Return the repository root directory.

    Resolves relative to this script's location (``scripts/`` sibling).
    """
    return Path(__file__).resolve().parents[2]


def set_hooks_path(root: Path) -> None:
    """Configure git to use repo-local hooks from ``.githooks/``.

    Args:
        root: The repository root directory.

    Raises:
        SystemExit: If the ``git config`` command fails.
    """
    completed = subprocess.run(
        ["git", "config", "core.hooksPath", ".githooks"],
        cwd=root,
        check=False,
    )
    if completed.returncode != 0:
        raise SystemExit("Failed to set git core.hooksPath for this clone.")


def ensure_hook_is_executable(root: Path) -> None:
    """Add user, group, and other execute bits to the pre-commit hook.

    Only meaningful on POSIX; a no-op on Windows since the hook path
    config alone is sufficient.

    Args:
        root: The repository root directory.
    """
    hook_path = root / ".githooks" / "pre-commit"
    current_mode = hook_path.stat().st_mode
    hook_path.chmod(current_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def install_submodule_hooks(root: Path) -> None:
    """Install repo-local hooks for each initialized source submodule.

    Args:
        root: The integrating prism-kit repository root.

    Raises:
        SystemExit: If a submodule is unavailable or its installer fails.
    """
    for name in SUBMODULES:
        submodule_root = root / name
        installer = submodule_root / "scripts" / "install_git_hooks.py"
        if not (submodule_root / ".git").exists() or not installer.is_file():
            raise SystemExit(
                f"The '{name}' submodule is missing or not initialized. "
                "Run 'git submodule update --init --recursive' and retry."
            )

        completed = subprocess.run(
            [sys.executable, str(installer)],
            cwd=submodule_root,
            check=False,
        )
        if completed.returncode != 0:
            raise SystemExit(f"Failed to install git hooks for '{name}'.")


def main() -> int:
    """Run the git hooks installer.

    Returns:
        0 on success, non-zero on failure.
    """
    root = repo_root()
    if not (root / ".git").exists():
        raise SystemExit("Run this installer from inside the git working tree.")

    set_hooks_path(root)
    if os.name != "nt":
        ensure_hook_is_executable(root)
    install_submodule_hooks(root)
    print("Installed repo-local git hooks from .githooks/.")
    return 0


def cli() -> int:
    """Run the hook installer with stdout and stderr logging."""
    if not __package__:
        sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from scripts.common.cli_logging import run_cli

    return run_cli("install-git-hooks", main)


if __name__ == "__main__":
    raise SystemExit(cli())
