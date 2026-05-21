# Prism Kit

## Brief Description

Prism Kit is a Zephyr firmware project for the Seeed XIAO SAMD21 that drives a
7-pixel WS2812 strip and a board status LED through a layered runtime.

The current target is the hardware-backed `HW` app backend. The firmware boots
through a staged handoff:

1. Zephyr initializes the kernel and devices.
2. OSHAL runs an `APPLICATION`-level `SYS_INIT()` hook to validate the board
   resources needed by this build.
3. `main()` in OSHAL calls the repo-owned `oshal_main_handoff()` bridge.
4. The bridge hands control to BAL, which initializes board-owned objects and
   launches the APP task.
5. APP initializes the Prism backend and publishes frames.
6. The `app_hw` task is the only task that mutates the BAL-owned WS2812 strip.

## Command Cheatsheet

Run commands from the repository root.

```bash
# Clean an existing configured build directory.
west build -t pristine

# Default clean rebuild. The script deletes build/ and runs a pristine build.
python scripts/build.py

# Clean rebuild with Zephyr debug-optimization overlays.
python scripts/build.py --debug-opt
python scripts/build.py --no-opt

# Flash the newest build artifact over J-Link.
python scripts/flash.py

# Flash a specific artifact.
python scripts/flash.py build/zephyr/zephyr.elf

# Verify the USB CDC ACM debug and command ports and their expected markers.
python scripts/smoke_test.py

# Smoke-test helpers.
python scripts/smoke_test.py --list-ports
python scripts/smoke_test.py --port COM7
```

Notes:

- `scripts/build.py` is the default build entrypoint for this repo. It removes
  `build/`, runs `west build -b seeeduino_xiao --pristine always .`, and
  refreshes the root `compile_commands.json`.
- `scripts/flash.py` uses SEGGER J-Link by default and resolves the newest
  supported artifact under `build/zephyr`.
- `scripts/smoke_test.py` discovers the Prism Kit CDC ACM ports and, by
  default, requires separate `DebugPort online on` and `CommandPort online on`
  banners so the debug and command channels can be distinguished.

## Architecture Summary

Prism Kit is split into four ownership boundaries.

- `include/prism/` defines the repo-owned logical strip contract used by APP.
- `oshal/` owns the Zephyr-facing boundary: early startup, tasking, time,
  debug-port access, and hardware-facing interfaces such as GPIO, PWM, and the
  WS2812 transport.
- `bal/` owns board policy and board resources, including the XIAO status LED,
  the fixed 7-pixel WS2812 strip, and the bootstrap boundary that launches APP.
- `app/` owns product logic and backend-specific runtime glue.

The steady-state execution path is intentionally narrow:

- `oshal/src/zephyr_system.c` validates board prerequisites and calls
  `oshal_main_handoff()` from `main()`.
- `src/boot_handoff_zephyr.cpp` bridges that C-shaped handoff into
  `bal::run_bootstrap(app::setup, app::loop)`.
- `bal/src/bootstrap.cpp` confirms OSHAL startup succeeded, initializes BAL
  resources, and creates the `app_main` task.
- `app/src/blink_app.cpp` initializes Prism during `app::setup()`, then
  `app::loop()` fills the logical strip with the next color, calls `show()`,
  and sleeps.
- `app/src/prism_hw_backend.cpp` stages frames in repo-owned Prism types.
- `app/src/app_hw.cpp` consumes committed frames, writes them into the BAL
  strip, flushes the physical LEDs, and toggles the board status LED.

This keeps APP free of Zephyr headers and devicetree details while preserving a
clear boot chain from Zephyr to OSHAL to BAL to APP.

## Setup/configuration Info

### Prerequisites

- Initialize submodules before building:

```bash
git submodule update --init --recursive
```

- Create a virtual environment and install the Python packages used by the repo
  scripts:

```bash
python -m venv .venv
source .venv/Scripts/activate
pip install --upgrade pip west jsonschema pyelftools pyserial
```

- Install the GNU Arm Embedded Toolchain. The build script expects version
  `14.2 rel1` by default at:

```text
C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1
```

  If it is installed elsewhere, set `GNUARMEMB_TOOLCHAIN_PATH` to the install
  root before building.

### West Workspace

Initialize the Zephyr workspace from this repository's manifest:

```bash
west init -l .
west update
west zephyr-export
```

`west init -l .` creates the west topdir in the parent directory of the repo,
so this project normally lives beside `zephyr/` inside the workspace.

### Board and Runtime Configuration

- The active Zephyr board target is `seeeduino_xiao`.
- `boards/seeeduino_xiao.overlay` routes the default debug console to one USB
  CDC ACM instance, exposes a second CDC ACM command channel, and enables the
  PA8 `TCC0/WO[0]` PWM output used by the WS2812 transport.
- BAL maps the current XIAO wiring as status LED on PA17 and WS2812 output on
  PA8 / `TCC0_WO0`.
- `prj.conf` enables C++17, USB CDC ACM console support, `printk`, and the
  minimal Newlib configuration used by this target.
- `prj.debug.conf` and `prj.noopt.conf` are selected through
  `scripts/build.py --debug-opt` and `scripts/build.py --no-opt`.
- `app/CMakeLists.txt` defaults `PRISM_KIT_APP_BACKEND` to `HW`. `SIM` is kept
  as a reserved future backend and is not implemented.