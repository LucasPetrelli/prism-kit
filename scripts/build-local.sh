#!/usr/bin/env bash

set -euo pipefail

# Resolve paths relative to the repository root so the script can be run from
# any working directory within a Git Bash session.
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
python_exe="$repo_root/.venv/Scripts/python.exe"

if [[ ! -x "$python_exe" ]]; then
  echo "Missing Python environment at '$python_exe'."
  exit 1
fi

# Convert the GNU Arm toolchain install root to a DOS short path so Zephyr's
# linker search paths do not split on Program Files spaces. Using PowerShell
# here avoids the cmd.exe quoting behavior that can hang when invoked from
# Git Bash command substitution.
toolchain_root="$(powershell.exe -NoProfile -Command "(New-Object -ComObject Scripting.FileSystemObject).GetFolder('C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1').ShortPath" | tr -d '\r')"

if [[ -z "$toolchain_root" || ! -x "$toolchain_root/bin/arm-none-eabi-gcc.exe" ]]; then
  echo "GNU Arm Embedded Toolchain 14.2 rel1 was not found at the expected install path."
  echo "Install it first, then rerun this script."
  exit 1
fi

export ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb
export GNUARMEMB_TOOLCHAIN_PATH="$toolchain_root"

cd "$repo_root"

# If the repository was moved, the existing CMake cache can still point at the
# old workspace root. Remove the build directory up front so west starts from a
# clean tree instead of trying to pristine a stale cache.
rm -rf "$repo_root/build"

"$python_exe" -m west build -b seeeduino_xiao --pristine always .