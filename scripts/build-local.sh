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

"$python_exe" -m west build -b seeeduino_xiao --pristine always . -- -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Refresh the root compile database with a clang-friendly variant derived from
# Zephyr's GCC-oriented commands. Keep the build directory output untouched so
# the actual build still reflects the compiler flags CMake generated.
"$python_exe" - <<'PY'
import json
import re
from pathlib import Path, PureWindowsPath

repo_root = Path.cwd()
build_db = repo_root / "build" / "compile_commands.json"
root_db = repo_root / "compile_commands.json"

gcc_only_patterns = (
  r"\s+-mfp16-format=\S+",
  r"\s+-fno-reorder-functions",
  r"\s+--param=\S+",
  r"\s+-fno-defer-pop",
)

entries = json.loads(build_db.read_text())
sanitized_entries = []

for entry in entries:
  command = entry.get("command")
  if command is None:
    raise RuntimeError("Expected Zephyr compile_commands.json entries to contain 'command'.")

  compiler, _, remainder = command.partition(" ")
  if not remainder:
    raise RuntimeError("Expected compiler command followed by arguments.")

  for pattern in gcc_only_patterns:
    command = re.sub(pattern, "", command)

  command = command.replace(
    ".exe ",
    ".exe --target=arm-none-eabi ",
    1,
  )

  compiler_path = PureWindowsPath(compiler)
  toolchain_root = Path(compiler_path.parent.parent)
  cpp_include_root = toolchain_root / "arm-none-eabi" / "include" / "c++"
  cpp_versions = sorted(path.name for path in cpp_include_root.iterdir() if path.is_dir())
  if cpp_versions:
    cpp_version = cpp_versions[-1]
    cpp_include_flags = (
      f" -isystem {toolchain_root / 'arm-none-eabi' / 'include' / 'c++' / cpp_version}"
      f" -isystem {toolchain_root / 'arm-none-eabi' / 'include' / 'c++' / cpp_version / 'arm-none-eabi'}"
      f" -isystem {toolchain_root / 'arm-none-eabi' / 'include' / 'c++' / cpp_version / 'backward'}"
      f" -isystem {toolchain_root / 'lib' / 'gcc' / 'arm-none-eabi' / cpp_version / 'include'}"
      f" -isystem {toolchain_root / 'lib' / 'gcc' / 'arm-none-eabi' / cpp_version / 'include-fixed'}"
      f" -isystem {toolchain_root / 'arm-none-eabi' / 'include'}"
    )
    if compiler.endswith("g++.exe") and cpp_include_flags not in command:
      command = command.replace("--target=arm-none-eabi", f"--target=arm-none-eabi{cpp_include_flags}", 1)

  sanitized_entry = {
    "directory": entry["directory"],
    "file": entry["file"],
    "command": command,
  }
  if "output" in entry:
    sanitized_entry["output"] = entry["output"]
  sanitized_entries.append(sanitized_entry)

root_db.write_text(json.dumps(sanitized_entries, indent=2) + "\n")
PY