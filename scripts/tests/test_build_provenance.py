"""Tests for successful-build provenance creation and validation."""

from __future__ import annotations

import json
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase

from scripts import build_provenance, flash


class BuildProvenanceTest(TestCase):
    """Verify atomic provenance records describe the current build contents."""

    def create_successful_build(
        self,
        root: Path,
        board_target: str = "seeeduino_xiao/samd21g18a",
    ) -> build_provenance.BuildProvenance:
        """Create temporary config and artifact files and publish their record."""
        config_path = build_provenance.build_config_path(root)
        config_path.parent.mkdir(parents=True)
        config_path.write_text(
            f'CONFIG_BOARD_TARGET="{board_target}"\n'
            "CONFIG_FLASH_BASE_ADDRESS=0x0\n"
            "CONFIG_FLASH_LOAD_OFFSET=0x2000\n",
            encoding="utf-8",
        )
        artifact = config_path.parent / "zephyr.elf"
        artifact.write_bytes(b"successful test image")
        return build_provenance.write_build_provenance(root, board_target)

    def test_write_records_target_default_path_and_content_fingerprints(self) -> None:
        """Goal: record the exact default artifact and verified board config.

        Steps: create a temporary successful build, load the generated JSON,
        and compare its target, project-relative path, and SHA-256 fields.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            provenance = self.create_successful_build(root)
            manifest = json.loads(
                build_provenance.provenance_path(root).read_text(encoding="utf-8")
            )

            self.assertEqual(manifest["schema_version"], 1)
            self.assertEqual(manifest["board_target"], "seeeduino_xiao/samd21g18a")
            self.assertEqual(manifest["artifact_path"], "build/zephyr/zephyr.elf")
            self.assertEqual(
                manifest["artifact_sha256"],
                build_provenance.sha256_file(provenance.artifact_path),
            )
            self.assertEqual(
                manifest["config_sha256"],
                build_provenance.sha256_file(build_provenance.build_config_path(root)),
            )

    def test_load_accepts_matching_manifest_and_files(self) -> None:
        """Goal: accept a record while both fingerprinted build files match.

        Steps: publish a temporary manifest, reload it through the validator,
        and verify the fully qualified target and resolved artifact are returned.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            written = self.create_successful_build(root)

            loaded = build_provenance.load_build_provenance(root)

            self.assertEqual(loaded, written)

    def test_write_rejects_selected_target_mismatch(self) -> None:
        """Goal: prevent a build from recording a config for another board.

        Steps: create config and artifact files for the Sense target, request a
        SAMD21 record, and verify publication fails without a manifest.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            config_path = build_provenance.build_config_path(root)
            config_path.parent.mkdir(parents=True)
            config_path.write_text(
                'CONFIG_BOARD_TARGET="xiao_ble/nrf52840/sense"\n',
                encoding="utf-8",
            )
            (config_path.parent / "zephyr.elf").write_bytes(b"image")

            with self.assertRaisesRegex(
                build_provenance.BuildProvenanceError, "Build target mismatch"
            ):
                build_provenance.write_build_provenance(
                    root, "seeeduino_xiao/samd21g18a"
                )

            self.assertFalse(build_provenance.provenance_path(root).exists())

    def test_missing_manifest_fails(self) -> None:
        """Goal: require a successful-build record for implicit flashing.

        Steps: validate a temporary project root without a manifest and verify
        the diagnostic directs the user to build successfully.
        """
        with TemporaryDirectory() as directory:
            with self.assertRaisesRegex(FileNotFoundError, "successful project build"):
                build_provenance.load_build_provenance(Path(directory))

    def test_malformed_manifest_fails(self) -> None:
        """Goal: reject invalid JSON instead of inferring current build metadata.

        Steps: write malformed manifest text and verify loading reports an
        actionable malformed-record error.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            manifest_path = build_provenance.provenance_path(root)
            manifest_path.parent.mkdir(parents=True)
            manifest_path.write_text("{not-json", encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "malformed"):
                build_provenance.load_build_provenance(root)

    def test_unsupported_schema_fails(self) -> None:
        """Goal: fail closed when a manifest version is not supported.

        Steps: publish a valid record, change only its schema version, and
        verify the validator rejects it before accepting build metadata.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.create_successful_build(root)
            manifest_path = build_provenance.provenance_path(root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["schema_version"] = 2
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "schema version"):
                build_provenance.load_build_provenance(root)

    def test_unsupported_target_fails_flash_device_validation(self) -> None:
        """Goal: keep valid provenance from selecting an unsupported J-Link.

        Steps: publish a content-valid manifest with an unknown target and
        verify the flash-side device mapping rejects it.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.create_successful_build(root, "unsupported/board")
            loaded = build_provenance.load_build_provenance(root)

            with self.assertRaisesRegex(ValueError, "Unsupported CONFIG_BOARD_TARGET"):
                flash.resolve_jlink_device(None, loaded.board_target)

    def test_target_mismatch_with_current_config_fails(self) -> None:
        """Goal: reject a manifest when the config now names another target.

        Steps: publish a SAMD21 record, replace CONFIG_BOARD_TARGET with the
        Sense target, and verify validation reports the target mismatch.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.create_successful_build(root)
            config_path = build_provenance.build_config_path(root)
            config_path.write_text(
                'CONFIG_BOARD_TARGET="xiao_ble/nrf52840/sense"\n',
                encoding="utf-8",
            )

            with self.assertRaisesRegex(ValueError, "target mismatch"):
                build_provenance.load_build_provenance(root)

    def test_stale_artifact_fingerprint_fails(self) -> None:
        """Goal: detect an artifact overwritten after successful publication.

        Steps: publish a record, change the artifact bytes, and verify the
        loader rejects implicit-flash provenance as stale.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            provenance = self.create_successful_build(root)
            provenance.artifact_path.write_bytes(b"overwritten after build")

            with self.assertRaisesRegex(ValueError, "artifact.*stale"):
                build_provenance.load_build_provenance(root)

    def test_stale_config_fingerprint_fails(self) -> None:
        """Goal: detect config changes even when the board target is unchanged.

        Steps: publish a record, append another Kconfig value, and verify the
        config fingerprint invalidates implicit flashing.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.create_successful_build(root)
            config_path = build_provenance.build_config_path(root)
            with config_path.open("a", encoding="utf-8") as handle:
                handle.write("CONFIG_CHANGED_AFTER_BUILD=y\n")

            with self.assertRaisesRegex(ValueError, "config.*stale"):
                build_provenance.load_build_provenance(root)

    def test_artifact_path_outside_build_output_fails(self) -> None:
        """Goal: prevent a manifest from binding unrelated files as build output.

        Steps: publish a valid record, replace its path with a project-relative
        path outside build/zephyr, and verify validation fails closed.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            self.create_successful_build(root)
            manifest_path = build_provenance.provenance_path(root)
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["artifact_path"] = "README.md"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "outside the project build"):
                build_provenance.load_build_provenance(root)
