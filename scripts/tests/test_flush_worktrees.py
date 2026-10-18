"""Host tests for the flush-worktrees command."""

from __future__ import annotations

from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
from unittest import TestCase
from unittest.mock import patch

from scripts.maintenance import flush_worktrees


class FlushWorktreesTest(TestCase):
    """Verify worktree selection, removal arguments, and failure handling."""

    def test_parses_nul_delimited_worktree_paths(self) -> None:
        """Goal: parse paths containing spaces from Git's porcelain output.

        Steps: provide two NUL-delimited worktree records, then verify both
        paths are recovered without including other record fields.
        """
        output = (
            "worktree C:/repo/main\0HEAD abc\0branch refs/heads/main\0\0"
            "worktree C:/repo/linked tree\0HEAD def\0detached\0\0"
        )

        self.assertEqual(
            flush_worktrees.parse_worktree_paths(output),
            [Path("C:/repo/main"), Path("C:/repo/linked tree")],
        )

    def test_removes_other_worktrees_and_preserves_the_active_one(self) -> None:
        """Goal: remove all listed linked worktrees, but keep the active one.

        Steps: mock Git's active root and a listing with two linked paths, run
        the command without force, then verify only those two paths are removed.
        """
        with TemporaryDirectory() as temporary_directory:
            active = Path(temporary_directory) / "active"
            first = Path(temporary_directory) / "linked one"
            second = Path(temporary_directory) / "linked two"
            active.mkdir()
            listing = (
                f"worktree {active}\0HEAD abc\0branch refs/heads/main\0\0"
                f"worktree {first}\0HEAD def\0detached\0\0"
                f"worktree {second}\0HEAD ghi\0detached\0\0"
            )
            git_results = [
                subprocess.CompletedProcess([], 0, stdout=f"{active}\n", stderr=""),
                subprocess.CompletedProcess([], 0, stdout=listing, stderr=""),
                subprocess.CompletedProcess([], 0),
                subprocess.CompletedProcess([], 0),
            ]

            with (
                patch.object(
                    flush_worktrees, "resolve_project_root", return_value=active
                ),
                patch("sys.argv", ["flush-worktrees"]),
                patch(
                    "scripts.maintenance.flush_worktrees.subprocess.run",
                    side_effect=git_results,
                ) as run_git,
                redirect_stdout(StringIO()),
            ):
                result = flush_worktrees.main()

        self.assertEqual(result, 0)
        self.assertEqual(
            [call.args[0] for call in run_git.call_args_list],
            [
                ["git", "rev-parse", "--show-toplevel"],
                ["git", "worktree", "list", "--porcelain", "-z"],
                ["git", "worktree", "remove", str(first)],
                ["git", "worktree", "remove", str(second)],
            ],
        )

    def test_passes_force_to_each_removal_and_reports_failures(self) -> None:
        """Goal: forward --force and continue after a failed worktree removal.

        Steps: mock two linked worktrees with the first removal failing, run
        with --force, then verify both removals receive the option and the
        command returns a failure status.
        """
        with TemporaryDirectory() as temporary_directory:
            active = Path(temporary_directory) / "active"
            first = Path(temporary_directory) / "linked one"
            second = Path(temporary_directory) / "linked two"
            active.mkdir()
            listing = (
                f"worktree {active}\0HEAD abc\0branch refs/heads/main\0\0"
                f"worktree {first}\0HEAD def\0detached\0\0"
                f"worktree {second}\0HEAD ghi\0detached\0\0"
            )
            git_results = [
                subprocess.CompletedProcess([], 0, stdout=f"{active}\n", stderr=""),
                subprocess.CompletedProcess([], 0, stdout=listing, stderr=""),
                subprocess.CompletedProcess([], 1),
                subprocess.CompletedProcess([], 0),
            ]

            with (
                patch.object(
                    flush_worktrees, "resolve_project_root", return_value=active
                ),
                patch("sys.argv", ["flush-worktrees", "--force"]),
                patch(
                    "scripts.maintenance.flush_worktrees.subprocess.run",
                    side_effect=git_results,
                ) as run_git,
                redirect_stdout(StringIO()),
                redirect_stderr(StringIO()),
            ):
                result = flush_worktrees.main()

        self.assertEqual(result, 1)
        self.assertEqual(
            [call.args[0] for call in run_git.call_args_list[2:]],
            [
                ["git", "worktree", "remove", "--force", str(first)],
                ["git", "worktree", "remove", "--force", str(second)],
            ],
        )

    def test_refuses_to_remove_worktrees_when_active_path_is_unlisted(self) -> None:
        """Goal: fail closed if the active checkout cannot be identified.

        Steps: report an active path absent from Git's worktree listing, then
        verify the command fails without invoking any removal operation.
        """
        with TemporaryDirectory() as temporary_directory:
            active = Path(temporary_directory) / "active"
            other = Path(temporary_directory) / "other"
            active.mkdir()
            listing = f"worktree {other}\0HEAD abc\0detached\0\0"
            git_results = [
                subprocess.CompletedProcess([], 0, stdout=f"{active}\n", stderr=""),
                subprocess.CompletedProcess([], 0, stdout=listing, stderr=""),
            ]

            with (
                patch.object(
                    flush_worktrees, "resolve_project_root", return_value=active
                ),
                patch("sys.argv", ["flush-worktrees"]),
                patch(
                    "scripts.maintenance.flush_worktrees.subprocess.run",
                    side_effect=git_results,
                ) as run_git,
                redirect_stderr(StringIO()),
            ):
                result = flush_worktrees.main()

        self.assertEqual(result, 1)
        self.assertEqual(run_git.call_count, 2)

    def test_reports_when_git_is_missing(self) -> None:
        """Goal: report an actionable error when the Git executable is absent.

        Steps: make the initial Git invocation raise FileNotFoundError, then
        verify the command exits unsuccessfully without attempting removals.
        """
        with TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            error_output = StringIO()

            with (
                patch.object(
                    flush_worktrees, "resolve_project_root", return_value=root
                ),
                patch("sys.argv", ["flush-worktrees"]),
                patch(
                    "scripts.maintenance.flush_worktrees.subprocess.run",
                    side_effect=FileNotFoundError("git"),
                ) as run_git,
                redirect_stderr(error_output),
            ):
                result = flush_worktrees.main()

        self.assertEqual(result, 1)
        self.assertEqual(run_git.call_count, 1)
        self.assertIn("Git was not found on PATH", error_output.getvalue())
