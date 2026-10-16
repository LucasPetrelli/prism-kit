"""Configure-level tests for Step 0 OSHAL demo selection behavior."""

from __future__ import annotations

from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
import textwrap
from unittest import TestCase

from scripts.unit_tests import default_cmake_env, find_build_tool

OSHAL_ROOT = Path(__file__).resolve().parents[2] / "oshal"


class OshalDemoSelectionTest(TestCase):
    """Verify reserved demos fail in the real OSHAL CMake dispatcher."""

    def configure_demo(self, demo_name: str) -> subprocess.CompletedProcess[str]:
        """Configure OSHAL through a minimal Zephyr-target fixture.

        Args:
            demo_name: Demo selector to pass through the CMake cache.

        Returns:
            CMake's captured configure result.
        """
        cmake = shutil.which("cmake")
        if cmake is None:
            self.skipTest("CMake is required for configure-level demo checks.")
        generator = find_build_tool()
        if not generator:
            self.skipTest("No supported host CMake generator is installed.")

        with TemporaryDirectory() as directory:
            root = Path(directory)
            source_dir = root / "fixture"
            source_dir.mkdir()
            (source_dir / "CMakeLists.txt").write_text(
                textwrap.dedent(
                    """
                    cmake_minimum_required(VERSION 3.20)
                    project(oshal_demo_selection_test LANGUAGES C CXX)
                    add_library(zephyr_interface INTERFACE)
                    set(CONFIG_SOC_FAMILY_ATMEL_SAM0 ON)
                    set(CONFIG_SOC_SERIES_SAMD21 ON)
                    add_subdirectory("${OSHAL_SOURCE_DIR}" oshal)
                    """
                ),
                encoding="utf-8",
            )
            return subprocess.run(
                [
                    cmake,
                    "-S",
                    str(source_dir),
                    "-B",
                    str(root / "build"),
                    "-G",
                    generator,
                    f"-DOSHAL_SOURCE_DIR={OSHAL_ROOT}",
                    f"-DOSHAL_DEMO={demo_name}",
                ],
                cwd=source_dir,
                env=default_cmake_env(),
                check=False,
                capture_output=True,
                text=True,
            )

    def assert_reserved_demo_rejected(
        self,
        demo_name: str,
        expected_message: str,
    ) -> None:
        """Assert CMake rejects a reserved selector with its Step diagnostic.

        Args:
            demo_name: Reserved OSHAL demo selector.
            expected_message: Stable diagnostic text expected in CMake output.
        """
        result = self.configure_demo(demo_name)
        output = result.stdout + result.stderr

        self.assertNotEqual(result.returncode, 0, output)
        self.assertIn(expected_message, output)

    def test_blinky_is_reserved_until_step_one(self) -> None:
        """Goal: keep Step 1's GPIO demo out of the Step 0 implementation.

        Steps: configure the real OSHAL dispatcher with ``blinky`` and verify
        CMake fails with the explicit Step 1 reservation diagnostic.
        """
        self.assert_reserved_demo_rejected(
            "blinky", "OSHAL_DEMO='blinky' is reserved but not implemented until Step 1."
        )

    def test_ws2812_spi_is_reserved_until_step_two(self) -> None:
        """Goal: keep Step 2's transport demo out of the Step 0 implementation.

        Steps: configure the real OSHAL dispatcher with ``ws2812-spi`` and
        verify CMake fails with the explicit Step 2 reservation diagnostic.
        """
        self.assert_reserved_demo_rejected(
            "ws2812-spi",
            "OSHAL_DEMO='ws2812-spi' is reserved but not implemented until Step 2.",
        )
