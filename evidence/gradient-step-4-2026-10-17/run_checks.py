#!/usr/bin/env python3
"""Run and record the requested Step 4 validation commands."""

from __future__ import annotations

from datetime import datetime, timezone
import json
from pathlib import Path
import subprocess
import sys

EVIDENCE_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = EVIDENCE_DIR.parents[1]
CHECKS = (
    (
        "focused-python-tests",
        [
            "uv",
            "run",
            "python",
            "-m",
            "unittest",
            "discover",
            "-s",
            "scripts/tests",
            "-p",
            "test_gradient.py",
        ],
    ),
    ("python-lint", ["uv", "run", "lint-python"]),
    ("project-lint", ["uv", "run", "lint"]),
    ("project-host-tests", ["uv", "run", "unit-tests"]),
    ("format-check", ["uv", "run", "format", "--check"]),
    (
        "xiao-nrf52-sense-build",
        ["uv", "run", "build", "--board", "xiao_nrf52_sense", "--clean"],
    ),
    ("diff-check", ["git", "diff", "--check"]),
)


def write_manifest(manifest: dict[str, object]) -> None:
    """Write the current command records for review, including in-progress work."""
    (EVIDENCE_DIR / "manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    """Run every check and return nonzero if any command fails."""
    EVIDENCE_DIR.mkdir(parents=True, exist_ok=True)
    manifest: dict[str, object] = {
        "step": 4,
        "project": str(PROJECT_ROOT),
        "checks": [],
    }
    records = manifest["checks"]
    assert isinstance(records, list)

    for name, argv in CHECKS:
        log_path = EVIDENCE_DIR / f"{name}.log"
        record: dict[str, object] = {
            "name": name,
            "command": " ".join(argv),
            "argv": argv,
            "working_directory": str(PROJECT_ROOT),
            "started_at_utc": datetime.now(timezone.utc).isoformat(),
            "exit_code": None,
            "raw_log": log_path.name,
        }
        records.append(record)
        write_manifest(manifest)
        print(f"RUN {record['command']} (cwd={PROJECT_ROOT})", flush=True)

        try:
            with log_path.open("wb") as log_file:
                completed = subprocess.run(
                    argv,
                    cwd=PROJECT_ROOT,
                    stdout=log_file,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
            exit_code = completed.returncode
        except OSError as exc:
            with log_path.open("ab") as log_file:
                log_file.write(f"{type(exc).__name__}: {exc}\n".encode())
            exit_code = 127

        record["exit_code"] = exit_code
        record["finished_at_utc"] = datetime.now(timezone.utc).isoformat()
        write_manifest(manifest)
        print(f"DONE {name} exit_code={exit_code}; raw_log={log_path}", flush=True)

    failed = [record for record in records if record["exit_code"] != 0]
    print(f"RESULT {len(records) - len(failed)}/{len(records)} commands passed")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
