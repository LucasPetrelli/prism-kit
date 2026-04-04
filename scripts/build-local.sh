#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
python_exe="$repo_root/.venv/Scripts/python.exe"

if [[ ! -x "$python_exe" ]]; then
  echo "Missing Python environment at '$python_exe'."
  exit 1
fi

exec "$python_exe" "$script_dir/build-local.py" "$@"