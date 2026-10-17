"""Configure-level tests for OSHAL demo and backend selection behavior."""

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
    """Verify the real OSHAL CMake dispatcher selects compatible demos."""

    def configure_demo(
        self, demo_name: str, soc: str = "samd21"
    ) -> subprocess.CompletedProcess[str]:
        """Configure OSHAL through a minimal Zephyr-target fixture.

        Args:
            demo_name: Demo selector to pass through the CMake cache.
            soc: SoC fixture name, either ``samd21`` or ``nrf52``.

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
                textwrap.dedent("""
                    cmake_minimum_required(VERSION 3.20)
                    project(oshal_demo_selection_test LANGUAGES C CXX)
                    add_library(zephyr_interface INTERFACE)
                    if(OSHAL_TEST_SOC STREQUAL "nrf52")
                        set(CONFIG_SOC_FAMILY_NORDIC_NRF ON)
                        set(CONFIG_SOC_SERIES_NRF52X ON)
                    else()
                        set(CONFIG_SOC_FAMILY_ATMEL_SAM0 ON)
                        set(CONFIG_SOC_SERIES_SAMD21 ON)
                    endif()
                    add_subdirectory("${OSHAL_SOURCE_DIR}" oshal)
                    message(STATUS
                        "OSHAL_ENABLE_COMMAND_USB_CDC=${OSHAL_ENABLE_COMMAND_USB_CDC}")
                    """),
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
                    f"-DOSHAL_TEST_SOC={soc}",
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

    def test_blinky_is_supported_for_samd21_gpio0_profile(self) -> None:
        """Goal: allow the generic GPIO demo on the SAMD21 profile.

        Steps: configure the real OSHAL dispatcher with ``blinky`` and verify
        the existing GPIO-capable backend accepts the generic demo.
        """
        result = self.configure_demo("blinky", "samd21")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_blinky_is_supported_for_nrf52(self) -> None:
        """Goal: route the generic GPIO demo to the new nRF52 backend.

        Steps: configure the real OSHAL dispatcher with ``blinky`` and verify
        the nRF52 backend is selected while the command-port option stays on.
        """
        result = self.configure_demo("blinky", "nrf52")
        output = result.stdout + result.stderr

        self.assertEqual(result.returncode, 0, output)
        self.assertIn("OSHAL_ENABLE_COMMAND_USB_CDC=ON", output)

    def test_samd21_dma_demo_rejects_nrf52_with_actionable_error(self) -> None:
        """Goal: explain the backend requirement for the SAMD21-only demo.

        Steps: configure ``samd21-dma-pwm`` with the nRF52 fixture and verify
        CMake fails with the selected demo's backend requirement.
        """
        result = self.configure_demo("samd21-dma-pwm", "nrf52")
        output = result.stdout + result.stderr

        self.assertNotEqual(result.returncode, 0, output)
        self.assertIn(
            "OSHAL_DEMO='samd21-dma-pwm' requires the SAMD21 backend.", output
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
