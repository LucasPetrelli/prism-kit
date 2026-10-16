"""Tests for first-run board selection and local preference persistence."""

from __future__ import annotations

import argparse
from contextlib import redirect_stderr
import json
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase
from unittest.mock import patch

from scripts import build, build_provenance


class BuildBoardSelectionTest(TestCase):
    """Verify the board CLI contract without starting West or using user state."""

    def run_mocked_build_pipeline(self, root: Path, lint_result: int = 0) -> int:
        """Run main with temporary outputs and all external build tools mocked."""
        config_path = build_provenance.build_config_path(root)
        config_path.parent.mkdir(parents=True)
        config_path.write_text(
            'CONFIG_BOARD_TARGET="seeeduino_xiao/samd21g18a"\n',
            encoding="utf-8",
        )
        (config_path.parent / "zephyr.elf").write_bytes(b"successful image")
        args = argparse.Namespace(
            board="seeeduino_xiao",
            no_opt=False,
            debug_opt=False,
            clean=False,
            oshal_demo="none",
            skip_lint=False,
        )
        with (
            patch.object(build, "repo_root", return_value=root),
            patch.object(build, "parse_args", return_value=args),
            patch.object(build, "resolve_python_with_west", return_value=Path("python")),
            patch.object(build, "ensure_required_submodules"),
            patch.object(build, "resolve_toolchain_root", return_value=Path("toolchain")),
            patch.object(build, "run_west_build"),
            patch.object(build, "sanitize_compile_database"),
            patch.object(build, "run_lint", return_value=lint_result) as run_lint,
            patch.object(build.os, "chdir"),
        ):
            result = build.main()

        run_lint.assert_called_once_with(root)
        return result

    def test_samd21_alias_maps_to_qualified_config_target(self) -> None:
        """Goal: verify the SAMD21 alias's fully qualified Kconfig target.

        Steps: resolve the selected alias through the build target map and
        compare it with the CONFIG_BOARD_TARGET emitted by Zephyr.
        """
        self.assertEqual(
            build.config_board_target_for_alias("seeeduino_xiao"),
            "seeeduino_xiao/samd21g18a",
        )

    def test_sense_alias_maps_to_qualified_config_target(self) -> None:
        """Goal: verify the Sense alias's fully qualified Kconfig target.

        Steps: resolve the selected alias through the build target map and
        compare it with the configured XIAO BLE Sense board target.
        """
        self.assertEqual(
            build.config_board_target_for_alias("xiao_nrf52_sense"),
            "xiao_ble/nrf52840/sense",
        )

    def test_first_use_without_board_fails_with_actionable_choices(self) -> None:
        """Goal: require a board on a fresh checkout instead of defaulting.

        Steps: resolve an absent temporary preference without an explicit alias
        and verify the error directs first use to the supported --board choices.
        """
        with TemporaryDirectory() as directory:
            settings_path = Path(directory) / build.LOCAL_SETTINGS_FILENAME

            with self.assertRaisesRegex(ValueError, "first use.*--board"):
                build.resolve_board(None, settings_path)

    def test_main_fails_before_west_when_first_use_omits_board(self) -> None:
        """Goal: stop a first-use build before locating or invoking West.

        Steps: point the wrapper at a temporary root with no saved choice, run
        its default CLI, then verify it returns an error without resolving West.
        """
        with TemporaryDirectory() as directory:
            error_output = StringIO()
            with (
                patch("sys.argv", ["build.py"]),
                patch.object(build, "repo_root", return_value=Path(directory)),
                patch.object(build, "resolve_python_with_west") as resolve_west,
                redirect_stderr(error_output),
            ):
                result = build.main()

        self.assertEqual(result, 2)
        resolve_west.assert_not_called()
        self.assertIn("--board", error_output.getvalue())

    def test_main_publishes_provenance_after_successful_pipeline(self) -> None:
        """Goal: publish build provenance only after all wrapper stages pass.

        Steps: run main with mocked build, sanitization, and successful lint
        stages, then validate the exact target and artifact record it published.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)

            result = self.run_mocked_build_pipeline(root)
            provenance = build_provenance.load_build_provenance(root)

        self.assertEqual(result, 0)
        self.assertEqual(provenance.board_target, "seeeduino_xiao/samd21g18a")
        self.assertEqual(provenance.artifact_path.name, "zephyr.elf")

    def test_main_does_not_publish_provenance_when_lint_fails(self) -> None:
        """Goal: leave no successful-build record when a required stage fails.

        Steps: run main with mocked build stages and a failing lint result, then
        verify the wrapper returns failure without writing a provenance file.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)

            result = self.run_mocked_build_pipeline(root, lint_result=1)

            self.assertEqual(result, 1)
            self.assertFalse(build_provenance.provenance_path(root).exists())

    def test_explicit_board_is_persisted_as_only_the_project_alias(self) -> None:
        """Goal: persist a validated alias without storing a demo or Zephyr name.

        Steps: choose the SAMD21 alias into a temporary settings file, then
        verify the returned Zephyr target and exact JSON preference contents.
        """
        with TemporaryDirectory() as directory:
            settings_path = Path(directory) / build.LOCAL_SETTINGS_FILENAME

            alias, target = build.resolve_board("seeeduino_xiao", settings_path)

            self.assertEqual((alias, target), ("seeeduino_xiao", "seeeduino_xiao"))
            self.assertEqual(
                json.loads(settings_path.read_text(encoding="utf-8")),
                {"board": "seeeduino_xiao"},
            )

    def test_saved_board_is_reused_without_rewriting_the_preference(self) -> None:
        """Goal: reuse the stored alias when --board is omitted later.

        Steps: seed a temporary file with the Sense alias, resolve without an
        override, and verify its Zephyr target and persisted bytes are unchanged.
        """
        with TemporaryDirectory() as directory:
            settings_path = Path(directory) / build.LOCAL_SETTINGS_FILENAME
            settings_path.write_text(
                json.dumps({"board": "xiao_nrf52_sense"}), encoding="utf-8"
            )
            original_contents = settings_path.read_text(encoding="utf-8")

            alias, target = build.resolve_board(None, settings_path)

            self.assertEqual(
                (alias, target),
                ("xiao_nrf52_sense", "xiao_ble/nrf52840/sense"),
            )
            self.assertEqual(settings_path.read_text(encoding="utf-8"), original_contents)

    def test_explicit_board_overrides_and_updates_a_saved_choice(self) -> None:
        """Goal: let an explicit alias replace the current local preference.

        Steps: seed the Sense alias, explicitly select SAMD21, and verify both
        the returned target and saved JSON now contain the SAMD21 project alias.
        """
        with TemporaryDirectory() as directory:
            settings_path = Path(directory) / build.LOCAL_SETTINGS_FILENAME
            settings_path.write_text(
                json.dumps({"board": "xiao_nrf52_sense"}), encoding="utf-8"
            )

            alias, target = build.resolve_board("seeeduino_xiao", settings_path)

            self.assertEqual((alias, target), ("seeeduino_xiao", "seeeduino_xiao"))
            self.assertEqual(
                json.loads(settings_path.read_text(encoding="utf-8")),
                {"board": "seeeduino_xiao"},
            )

    def test_invalid_saved_board_fails_instead_of_falling_back(self) -> None:
        """Goal: reject stale or malformed saved aliases without a default.

        Steps: write an unsupported alias to a temporary preference, resolve it,
        and verify the diagnostic asks the user to select with --board.
        """
        with TemporaryDirectory() as directory:
            settings_path = Path(directory) / build.LOCAL_SETTINGS_FILENAME
            settings_path.write_text(
                json.dumps({"board": "not-a-project-board"}), encoding="utf-8"
            )

            with self.assertRaisesRegex(ValueError, "Select a board with --board"):
                build.resolve_board(None, settings_path)

    def test_legacy_uppercase_demo_name_is_rejected(self) -> None:
        """Goal: prevent old uppercase demo names from being silently accepted.

        Steps: parse the legacy SAMD21 selector spelling and verify argparse
        reports it as an invalid choice rather than normalizing the value.
        """
        error_output = StringIO()
        with (
            patch("sys.argv", ["build.py", "--oshal-demo", "SAMD21_DMA_PWM"]),
            redirect_stderr(error_output),
        ):
            with self.assertRaises(SystemExit) as raised:
                build.parse_args()

        self.assertEqual(raised.exception.code, 2)
        self.assertIn("invalid choice", error_output.getvalue())

    def test_local_board_preference_survives_build_directory_cleanup(self) -> None:
        """Goal: keep the board preference outside the wrapper's clean target.

        Steps: create a temporary build output and local preference, call the
        build-directory cleanup helper, and verify only the build output is gone.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            build_output = root / "build" / "artifact.bin"
            settings_path = root / build.LOCAL_SETTINGS_FILENAME
            build_output.parent.mkdir(parents=True)
            build_output.write_bytes(b"generated")
            settings_path.write_text(
                json.dumps({"board": "seeeduino_xiao"}), encoding="utf-8"
            )

            build.remove_build_directory(root)

            self.assertFalse(build_output.exists())
            self.assertTrue(settings_path.is_file())

    def test_explicit_unknown_alias_is_rejected(self) -> None:
        """Goal: reject an unknown explicit alias before it reaches West.

        Steps: call the resolver with an unsupported project alias and verify
        the diagnostic lists valid board choices.
        """
        with TemporaryDirectory() as directory:
            settings_path = Path(directory) / build.LOCAL_SETTINGS_FILENAME

            with self.assertRaisesRegex(ValueError, "seeeduino_xiao"):
                build.resolve_board("unknown", settings_path)