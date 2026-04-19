#!/usr/bin/env python3

from __future__ import annotations

import os
import stat
import subprocess
from pathlib import Path


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def set_hooks_path(root: Path) -> None:
    completed = subprocess.run(
        ["git", "config", "core.hooksPath", ".githooks"],
        cwd=root,
        check=False,
    )
    if completed.returncode != 0:
        raise SystemExit("Failed to set git core.hooksPath for this clone.")


def ensure_hook_is_executable(root: Path) -> None:
    hook_path = root / ".githooks" / "pre-commit"
    current_mode = hook_path.stat().st_mode
    hook_path.chmod(current_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main() -> int:
    root = repo_root()
    if not (root / ".git").exists():
        raise SystemExit("Run this installer from inside the git working tree.")

    set_hooks_path(root)
    if os.name != "nt":
        ensure_hook_is_executable(root)
    print("Installed repo-local git hooks from .githooks/.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
