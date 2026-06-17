#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = []
# ///

"""
Compile and run project unit-tests outside of the Zephyr build system.

Usage::

    uv run unit-tests
    uv run unit-tests --clean
    uv run unit-tests --dir protocol
    uv run unit-tests --dir protocol --gtest_filter=Rx_*

All folders named ``tests/`` that contain a ``CMakeLists.txt`` are discovered
automatically and each is treated as a self-contained test suite.
GoogleTest is fetched via CMake FetchContent.

Any extra arguments after the script flags are forwarded directly to every
test binary (e.g. ``--gtest_filter=<pattern>``).
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

# ── Helpers ────────────────────────────────────────────────────────────


def resolve_project_root() -> Path:
    """Return the project root (directory containing scripts/)."""
    return Path(__file__).resolve().parent.parent


def find_build_tool() -> str:
    """
    Return the best available CMake build-tool flag for this platform.

    On Windows, prefers MinGW Makefiles so the native g++ compiler is used
    instead of clang targeting MSVC.
    """
    if sys.platform == "win32" and shutil.which("mingw32-make"):
        return "MinGW Makefiles"
    if shutil.which("ninja"):
        return "Ninja"
    if shutil.which("make"):
        return "Unix Makefiles"
    return ""


def default_cmake_env() -> dict[str, str]:
    """Return a CMake environment with sensible defaults for this platform."""
    env = os.environ.copy()
    if sys.platform == "win32":
        env.setdefault("CC", "gcc")
        env.setdefault("CXX", "g++")
    return env


# ── TestSuite ──────────────────────────────────────────────────────────


@dataclasses.dataclass
class TestSuite:
    """A single test suite backed by a ``tests/`` directory.

    Each suite owns its build directory and can build and run itself.
    """

    name: str
    """Human-readable suite name, derived from the parent directory name."""

    source_dir: Path
    """Directory containing the suite's ``CMakeLists.txt``."""

    build_dir: Path
    """Directory where CMake build artifacts are written."""

    # ── Public API ──────────────────────────────────────────────────

    def build(
        self,
        root: Path,
        generator: str,
        cmake_env: dict[str, str],
        clean: bool,
    ) -> bool:
        """Configure and build this suite.  Returns True on success."""
        self._clean_build_dir(clean)

        if not self._configure(root, generator, cmake_env):
            return False
        if not self._compile(root):
            return False
        return True

    def run(self, test_args: list[str]) -> bool:
        """Run all test binaries in this suite.  Returns True if all pass."""
        binaries = self._discover_binaries()
        if not binaries:
            print(f"  No test binaries found in {self.build_dir}.", file=sys.stderr)
            return False

        all_passed = True
        for binary in binaries:
            run_args = [str(binary)] + test_args
            print(f"  Running: {' '.join(run_args)}")
            result = subprocess.run(run_args, check=False)
            if result.returncode != 0:
                all_passed = False
        return all_passed

    # ── Private helpers ─────────────────────────────────────────────

    def _clean_build_dir(self, clean: bool) -> None:
        if not clean or not self.build_dir.exists():
            return
        print(f"Removing {self.build_dir} ...")

        def _remove_readonly(func, path, exc_info):
            import stat

            os.chmod(path, stat.S_IWRITE)
            func(path)

        shutil.rmtree(self.build_dir, onerror=_remove_readonly)

    def _configure(self, root: Path, generator: str, cmake_env: dict[str, str]) -> bool:
        self.build_dir.mkdir(parents=True, exist_ok=True)
        cmake_args = [
            "cmake",
            "-B",
            str(self.build_dir),
            "-S",
            str(self.source_dir),
        ]
        if generator:
            cmake_args.extend(["-G", generator])

        print(f"\n--- Configuring: {self.name} ---")
        print(f"  {' '.join(cmake_args)}")
        result = subprocess.run(cmake_args, cwd=str(root), env=cmake_env, check=False)
        if result.returncode != 0:
            print(
                f"  CMake configure failed for {self.name}.",
                file=sys.stderr,
            )
            return False
        return True

    def _compile(self, root: Path) -> bool:
        build_args = ["cmake", "--build", str(self.build_dir)]
        print(f"  Building: {' '.join(build_args)}")
        result = subprocess.run(build_args, cwd=str(root), check=False)
        if result.returncode != 0:
            print(f"  Build failed for {self.name}.", file=sys.stderr)
            return False
        return True

    def _discover_binaries(self) -> list[Path]:
        """Return paths to all executable test binaries in *build_dir*."""
        binaries: list[Path] = []
        suffix = ".exe" if sys.platform == "win32" else ""
        for entry in self.build_dir.iterdir():
            if not entry.is_file() or not entry.name.endswith(suffix):
                continue
            stem = entry.stem
            # Skip CMake admin files
            if stem in (
                "CMakeCache",
                "cmake_install",
                "Makefile",
                "CMakeDirectoryInformation",
            ):
                continue
            # Heuristic: only binaries with "test" in the name
            if "test" in stem.lower():
                binaries.append(entry)
        return binaries


# ── Discovery ──────────────────────────────────────────────────────────


def discover_test_suites(root: Path, dir_filter: str | None) -> list[TestSuite]:
    """Walk *root* for folders named ``tests/`` with ``CMakeLists.txt``."""
    suites: list[TestSuite] = []

    for tests_dir in _iter_test_dirs(root):
        cmake_path = tests_dir / "CMakeLists.txt"
        if not cmake_path.exists():
            continue
        name = _suite_name(root, tests_dir)
        build_dir = tests_dir / "build"
        suites.append(TestSuite(name, tests_dir, build_dir))

    if dir_filter:
        before = suites[:]
        suites = [s for s in suites if s.name == dir_filter]
        if not suites:
            available = ", ".join(s.name for s in before) or "(none)"
            print(
                f"error: no test suite named '{dir_filter}' "
                f"(available: {available})",
                file=sys.stderr,
            )
            return []

    return suites


def _iter_test_dirs(root: Path):
    """Yield every directory named ``tests`` under *root*."""
    # Depth-first to keep order reproducible: root-level first.
    for entry in sorted(root.iterdir()):
        if entry.is_dir() and entry.name == "tests":
            yield entry
    for entry in sorted(root.iterdir()):
        if entry.is_dir() and entry.name not in ("build", ".git", "_deps"):
            yield from _iter_test_dirs(entry)


def _suite_name(root: Path, tests_dir: Path) -> str:
    """Derive a human-readable suite name from the directory structure.

    Priority:
      1. A non-empty ``.suite-name`` file inside the tests directory.
      2. The ``project(NAME ...)`` from the suite's ``CMakeLists.txt``.
      3. The name of the directory that contains the ``tests/`` folder.
    """

    # 1. Explicit .suite-name file
    name_file = tests_dir / ".suite-name"
    if name_file.exists():
        content = name_file.read_text().strip()
        if content:
            return content

    # 2. project() name from CMakeLists.txt
    cmake_path = tests_dir / "CMakeLists.txt"
    proj = _parse_project_name(cmake_path)
    if proj:
        return proj

    # 3. Parent directory name
    return tests_dir.parent.name


def _parse_project_name(cmake_path: Path) -> str:
    """Extract the project name from a CMakeLists.txt, or return ''."""
    try:
        text = cmake_path.read_text()
    except OSError:
        return ""
    for line in text.splitlines():
        stripped = line.strip()
        # CMake project() syntax:
        #   project(name)
        #   project(name LANGUAGES ...)
        #   project(NAME name ...)
        m = re.match(r"project\s*\(\s*(?:NAME\s+)?(\w+)", stripped, re.IGNORECASE)
        if m:
            return m.group(1)
    return ""


# ── Entry point ────────────────────────────────────────────────────────


def main() -> int:
    root = resolve_project_root()

    parser = argparse.ArgumentParser(
        description="Build and run all project unit-test suites.",
    )
    parser.add_argument(
        "--dir",
        type=str,
        default=None,
        help=(
            "Run only the test suite with this name "
            "(e.g. 'controller_test', 'protocol')."
        ),
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove build directories before configuring.",
    )
    # Remaining args go to every test binary.
    args, test_args = parser.parse_known_args()

    suites = discover_test_suites(root, args.dir)
    if not suites:
        return 1

    generator = find_build_tool()
    cmake_env = default_cmake_env()

    passed = 0
    failed: list[str] = []

    for suite in suites:
        ok = suite.build(root, generator, cmake_env, args.clean)
        if not ok:
            failed.append(suite.name)
            continue
        ok = suite.run(test_args)
        if ok:
            passed += 1
        else:
            failed.append(suite.name)

    # Summary
    total = len(suites)
    print(f"\n{'=' * 40}")
    print(f"Results: {passed}/{total} test suites passed")
    if failed:
        print(f"Failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("All test suites passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
