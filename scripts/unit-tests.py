#!/usr/bin/env python3
"""
Compile and run the protocol unit-tests outside of the Zephyr build system.

Usage:
  python scripts/unit-tests.py [--build-dir PATH] [test args...]

The protocol module has no Zephyr dependencies so the tests compile with
just a host g++ (or clang++) and CMake.  GoogleTest is fetched automatically
via CMake FetchContent.

Any extra arguments after the script flags are forwarded directly to the
test binary (e.g. --gtest_filter=Rx_*).
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path


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


def main() -> int:
    root = resolve_project_root()

    parser = argparse.ArgumentParser(description="Build and run protocol unit tests.")
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=root / "protocol" / "tests" / "build",
        help="Build directory (default: protocol/tests/build/)",
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Remove the build directory before configuring.",
    )
    # Remaining args go to the test binary.
    args, test_args = parser.parse_known_args()

    build_dir: Path = args.build_dir
    source_dir = root / "protocol" / "tests"

    if not (source_dir / "CMakeLists.txt").exists():
        print(f"error: {source_dir / 'CMakeLists.txt'} not found", file=sys.stderr)
        return 1

    # ------------------------------------------------------------------
    # Clean
    # ------------------------------------------------------------------
    if args.clean and build_dir.exists():
        print(f"Removing {build_dir} ...")

        def _remove_readonly(func, path, exc_info):
            """Clear the read-only bit and retry the removal."""
            import stat

            os.chmod(path, stat.S_IWRITE)
            func(path)

        shutil.rmtree(build_dir, onerror=_remove_readonly)

    # ------------------------------------------------------------------
    # CMake configure
    # ------------------------------------------------------------------
    generator = find_build_tool()
    cmake_args = [
        "cmake",
        "-B",
        str(build_dir),
        "-S",
        str(source_dir),
    ]
    if generator:
        cmake_args.extend(["-G", generator])

    print(f"Configuring: {' '.join(cmake_args)}")
    cmake_env = os.environ.copy()
    if sys.platform == "win32":
        cmake_env.setdefault("CC", "gcc")
        cmake_env.setdefault("CXX", "g++")
    result = subprocess.run(cmake_args, cwd=str(root), env=cmake_env, check=False)
    if result.returncode != 0:
        print("CMake configure failed.", file=sys.stderr)
        return result.returncode

    # ------------------------------------------------------------------
    # CMake build
    # ------------------------------------------------------------------
    build_args = [
        "cmake",
        "--build",
        str(build_dir),
    ]
    print(f"Building: {' '.join(build_args)}")
    result = subprocess.run(build_args, cwd=str(root), check=False)
    if result.returncode != 0:
        print("Build failed.", file=sys.stderr)
        return result.returncode

    # ------------------------------------------------------------------
    # Run tests
    # ------------------------------------------------------------------
    test_exe = build_dir / "protocol_test"
    if sys.platform == "win32":
        test_exe = test_exe.with_suffix(".exe")

    if not test_exe.exists():
        print(f"error: test binary not found at {test_exe}", file=sys.stderr)
        return 1

    run_args = [str(test_exe)] + test_args
    print(f"Running: {' '.join(run_args)}")
    result = subprocess.run(run_args, cwd=str(root), check=False)

    if result.returncode == 0:
        print("\nAll tests passed.")
    else:
        print("\nSome tests failed.", file=sys.stderr)

    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
