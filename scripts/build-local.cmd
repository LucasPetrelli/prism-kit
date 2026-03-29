@echo off
setlocal

rem Resolve paths relative to the repository root so the script can be run from
rem any working directory on this machine.
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_ROOT=%%~fI"
set "PYTHON_EXE=%REPO_ROOT%\.venv\Scripts\python.exe"

if not exist "%PYTHON_EXE%" (
  echo Missing Python environment at "%PYTHON_EXE%".
  exit /b 1
)

rem Convert the GNU Arm toolchain install root to a short DOS path so Zephyr's
rem linker search paths do not split on Program Files spaces.
for %%I in ("C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1") do set "GNUARMEMB_TOOLCHAIN_PATH=%%~sI"

if not exist "%GNUARMEMB_TOOLCHAIN_PATH%\bin\arm-none-eabi-gcc.exe" (
  echo GNU Arm Embedded Toolchain 14.2 rel1 was not found at the expected install path.
  echo Install it first, then rerun this script.
  exit /b 1
)

set "ZEPHYR_TOOLCHAIN_VARIANT=gnuarmemb"

cd /d "%REPO_ROOT%"
"%PYTHON_EXE%" -m west build -b seeeduino_xiao --pristine always .