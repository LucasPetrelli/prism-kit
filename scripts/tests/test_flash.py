"""Tests for flash artifact metadata and J-Link device selection."""

from __future__ import annotations

import argparse
from contextlib import redirect_stderr
import json
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase
from unittest.mock import patch

from scripts.firmware import build_provenance, flash


class FlashBoardMetadataTest(TestCase):
    """Verify J-Link selection is bound to validated build provenance."""

    def create_successful_build(
        self,
        root: Path,
        board_target: str = "seeeduino_xiao/samd21g18a",
    ) -> build_provenance.BuildProvenance:
        """Create temporary build files and publish matching provenance."""
        config_path = build_provenance.build_config_path(root)
        config_path.parent.mkdir(parents=True)
        config_path.write_text(
            f'CONFIG_BOARD_TARGET="{board_target}"\n'
            "CONFIG_FLASH_BASE_ADDRESS=0x0\n"
            "CONFIG_FLASH_LOAD_OFFSET=0x8000\n",
            encoding="utf-8",
        )
        (config_path.parent / "zephyr.elf").write_bytes(b"successful test image")
        return build_provenance.write_build_provenance(root, board_target)

    def flash_args(
        self,
        artifact: str | None = None,
        device: str | None = None,
        load_address: int | None = None,
    ) -> argparse.Namespace:
        """Return the CLI arguments needed by request-resolution tests."""
        return argparse.Namespace(
            artifact=artifact,
            device=device,
            load_address=load_address,
            interface="SWD",
            speed="4000",
            dry_run=True,
        )

    def assert_main_rejects_before_jlink(
        self,
        root: Path,
        args: argparse.Namespace,
    ) -> str:
        """Run main against a temporary root and assert J-Link is not resolved."""
        error_output = StringIO()
        with (
            patch.object(flash, "REPO_ROOT", root),
            patch.object(flash, "parse_args", return_value=args),
            patch.object(flash, "resolve_jlink_executable") as resolve_jlink,
            redirect_stderr(error_output),
        ):
            result = flash.main()

        self.assertEqual(result, 1)
        resolve_jlink.assert_not_called()
        return error_output.getvalue()

    def test_samd21_target_selects_samd21_jlink_device(self) -> None:
        """Goal: map the qualified XIAO SAMD21 target to its J-Link device.

        Steps: resolve the supported Zephyr target name and verify the exact
        ATSAMD21G18A device string expected by SEGGER.
        """
        self.assertEqual(
            flash.jlink_device_for_board_target("seeeduino_xiao/samd21g18a"),
            "ATSAMD21G18A",
        )

    def test_nrf52_sense_target_selects_nrf52840_jlink_device(self) -> None:
        """Goal: map the Sense board target to the nRF52840 J-Link device.

        Steps: resolve the qualified XIAO BLE Sense target and verify the
        nRF52840_xxAA device name instead of reusing the SAMD21 default.
        """
        self.assertEqual(
            flash.jlink_device_for_board_target("xiao_ble/nrf52840/sense"),
            "nRF52840_xxAA",
        )

    def test_unsupported_target_fails_even_with_device_override(self) -> None:
        """Goal: prevent an override from bypassing unsupported board metadata.

        Steps: provide an unknown fully qualified target plus --device and
        verify the mapping still fails closed.
        """
        with self.assertRaisesRegex(ValueError, "Unsupported CONFIG_BOARD_TARGET"):
            flash.resolve_jlink_device("custom-device", "unsupported/board")

    def test_default_flash_uses_recorded_artifact_without_local_preference(
        self,
    ) -> None:
        """Goal: use the exact recorded image, not rediscovered output or preference.

        Steps: publish valid provenance, add an unrelated candidate artifact,
        resolve with no artifact/device override, and verify manifest selection.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            provenance = self.create_successful_build(root)
            (provenance.artifact_path.parent / "old.hex").write_bytes(b"other")

            artifact, device, config_path = flash.resolve_flash_request(
                self.flash_args(), root
            )

            self.assertEqual(artifact, provenance.artifact_path)
            self.assertEqual(device, "ATSAMD21G18A")
            self.assertEqual(config_path, build_provenance.build_config_path(root))
            self.assertFalse((root / ".prism-kit.local.json").exists())

    def test_device_override_does_not_bypass_missing_default_provenance(self) -> None:
        """Goal: require provenance for implicit flashing even with --device.

        Steps: run the default flash path against a root without a manifest,
        supply a device override, and verify J-Link lookup never occurs.
        """
        with TemporaryDirectory() as directory:
            error = self.assert_main_rejects_before_jlink(
                Path(directory), self.flash_args(device="ATSAMD21G18A")
            )

        self.assertIn("provenance", error.lower())

    def test_invalid_default_provenance_fails_before_jlink(self) -> None:
        """Goal: block J-Link for malformed, mismatched, unsupported, or stale data.

        Steps: independently corrupt the manifest schema, target, artifact, or
        config and verify the default path fails before resolving J-Link.
        """
        invalid_cases = (
            "malformed",
            "unsupported-schema",
            "unsupported-target",
            "target-mismatch",
            "stale-artifact",
            "stale-config",
        )
        for invalid_case in invalid_cases:
            with (
                self.subTest(invalid_case=invalid_case),
                TemporaryDirectory() as directory,
            ):
                root = Path(directory)
                if invalid_case == "malformed":
                    manifest_path = build_provenance.provenance_path(root)
                    manifest_path.parent.mkdir(parents=True)
                    manifest_path.write_text("{bad-json", encoding="utf-8")
                else:
                    target = (
                        "unsupported/board"
                        if invalid_case == "unsupported-target"
                        else "seeeduino_xiao/samd21g18a"
                    )
                    provenance = self.create_successful_build(root, target)
                    manifest_path = build_provenance.provenance_path(root)
                    if invalid_case == "unsupported-schema":
                        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                        manifest["schema_version"] = 2
                        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
                    elif invalid_case == "target-mismatch":
                        build_provenance.build_config_path(root).write_text(
                            'CONFIG_BOARD_TARGET="xiao_ble/nrf52840/sense"\n',
                            encoding="utf-8",
                        )
                    elif invalid_case == "stale-artifact":
                        provenance.artifact_path.write_bytes(b"changed after build")
                    elif invalid_case == "stale-config":
                        with build_provenance.build_config_path(root).open(
                            "a", encoding="utf-8"
                        ) as handle:
                            handle.write("CONFIG_CHANGED_AFTER_BUILD=y\n")

                error = self.assert_main_rejects_before_jlink(
                    root, self.flash_args(device="ATSAMD21G18A")
                )
                self.assertTrue(error.strip())

    def test_explicit_external_elf_requires_device(self) -> None:
        """Goal: avoid applying project metadata to an external ELF silently.

        Steps: resolve an outside artifact without --device and verify failure,
        then provide an explicit device and verify manual resolution succeeds.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory) / "project"
            root.mkdir()
            artifact = Path(directory) / "release.elf"
            artifact.write_bytes(b"external image")

            with self.assertRaisesRegex(ValueError, "--device"):
                flash.resolve_flash_request(
                    self.flash_args(artifact=str(artifact)), root
                )

            resolved_artifact, device, _ = flash.resolve_flash_request(
                self.flash_args(artifact=str(artifact), device="nRF52840_xxAA"),
                root,
            )

        self.assertEqual(resolved_artifact, artifact.resolve())
        self.assertEqual(device, "nRF52840_xxAA")

    def test_external_binary_requires_device_and_load_address(self) -> None:
        """Goal: require both identity and address for an unverified binary.

        Steps: create an external .bin and verify missing device or address is
        rejected before J-Link, then confirm explicit values are accepted.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory) / "project"
            root.mkdir()
            artifact = Path(directory) / "release.bin"
            artifact.write_bytes(b"external binary")

            with self.assertRaisesRegex(ValueError, "--device"):
                flash.resolve_flash_request(
                    self.flash_args(artifact=str(artifact)), root
                )
            error = self.assert_main_rejects_before_jlink(
                root,
                self.flash_args(
                    artifact=str(artifact), device="nRF52840_xxAA", load_address=None
                ),
            )
            self.assertIn("--load-address", error)

            resolved_artifact, device, _ = flash.resolve_flash_request(
                self.flash_args(
                    artifact=str(artifact),
                    device="nRF52840_xxAA",
                    load_address=0x12000,
                ),
                root,
            )

        self.assertEqual(resolved_artifact, artifact.resolve())
        self.assertEqual(device, "nRF52840_xxAA")

    def test_explicit_recorded_artifact_uses_verified_target(self) -> None:
        """Goal: preserve metadata use for an explicitly named recorded image.

        Steps: publish build provenance, pass the exact recorded artifact path,
        and verify the matching board target supplies the device mapping.
        """
        with TemporaryDirectory() as directory:
            root = Path(directory)
            provenance = self.create_successful_build(root)

            artifact, device, _ = flash.resolve_flash_request(
                self.flash_args(artifact=str(provenance.artifact_path)), root
            )

        self.assertEqual(artifact, provenance.artifact_path)
        self.assertEqual(device, "ATSAMD21G18A")

    def test_external_binary_load_command_uses_explicit_address(self) -> None:
        """Goal: keep an external binary independent of the project's config.

        Steps: build a load command with an explicit address and verify it uses
        that value without reading a project .config file.
        """
        with TemporaryDirectory() as directory:
            artifact = Path(directory) / "release.bin"
            command = flash.build_load_command(artifact, load_address=0x12000)

        self.assertEqual(command, f'loadfile "{artifact}" 0x12000')

    def test_project_binary_load_command_uses_its_config(self) -> None:
        """Goal: derive a verified project .bin address from Kconfig output.

        Steps: create a build-local config with flash base and offset values,
        resolve the load command, and verify their sum is used.
        """
        with TemporaryDirectory() as directory:
            build_dir = Path(directory) / "build" / "zephyr"
            build_dir.mkdir(parents=True)
            artifact = build_dir / "zephyr.bin"
            artifact.touch()
            config_path = build_dir / ".config"
            config_path.write_text(
                'CONFIG_BOARD_TARGET="seeeduino_xiao/samd21g18a"\n'
                "CONFIG_FLASH_BASE_ADDRESS=0x0\n"
                "CONFIG_FLASH_LOAD_OFFSET=0x8000\n",
                encoding="utf-8",
            )

            command = flash.build_load_command(artifact, config_path=config_path)

        self.assertEqual(command, f'loadfile "{artifact}" 0x8000')
