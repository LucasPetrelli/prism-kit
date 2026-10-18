"""Tests for per-run command output logging."""

from __future__ import annotations

from datetime import datetime as RealDatetime
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory
from unittest import TestCase
from unittest.mock import patch

from scripts.common import cli_logging


class CliLoggingTest(TestCase):
    """Verify CLI output capture, return-code naming, and collision handling."""

    def test_captures_python_and_child_process_output(self) -> None:
        """Goal: retain stdout and stderr from the command and its subprocess.

        Steps: print on both Python streams, write on both child process file
        descriptors, then verify the combined log and the command's exit code.
        """
        with TemporaryDirectory(dir=Path.cwd()) as temporary_directory:
            root = Path(temporary_directory)

            def command() -> int:
                print("parent stdout")
                print("parent stderr", file=sys.stderr)
                subprocess.run(
                    [
                        sys.executable,
                        "-c",
                        "import os; os.write(1, b'child stdout\\n'); "
                        "os.write(2, b'child stderr\\n')",
                    ],
                    check=True,
                )
                return 7

            return_code = cli_logging.run_cli(
                "sample-command", command, project_root=root
            )

            log_files = list((root / "log").glob("*.log"))
            self.assertEqual(return_code, 7)
            self.assertEqual(len(log_files), 1)
            self.assertRegex(
                log_files[0].name,
                r"^\d{8}-\d{6}-sample-command-7\.log$",
            )
            output = log_files[0].read_bytes()
            for expected in (
                b"parent stdout",
                b"parent stderr",
                b"child stdout",
                b"child stderr",
            ):
                self.assertIn(expected, output)

    def test_same_second_runs_keep_both_logs(self) -> None:
        """Goal: keep same-second executions as separate log files.

        Steps: freeze the logger clock, run the same named command twice, and
        verify that the second log receives a collision suffix.
        """
        fixed_time = RealDatetime(2026, 10, 18, 15, 30, 45)

        class FixedDatetime:
            @staticmethod
            def now() -> RealDatetime:
                return fixed_time

        with TemporaryDirectory(dir=Path.cwd()) as temporary_directory:
            root = Path(temporary_directory)
            with patch.object(cli_logging, "datetime", FixedDatetime):
                cli_logging.run_cli("repeat", lambda: 0, project_root=root)
                cli_logging.run_cli("repeat", lambda: 0, project_root=root)

            log_names = sorted(path.name for path in (root / "log").glob("*.log"))
            self.assertEqual(
                log_names,
                [
                    "20261018-153045-repeat-0-01.log",
                    "20261018-153045-repeat-0.log",
                ],
            )
