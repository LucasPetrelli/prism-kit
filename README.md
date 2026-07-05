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
# Build (incremental by default).
uv run build

# Build with Zephyr debug-optimization overlays.
uv run build --debug-opt
uv run build --no-opt

# Force a full clean rebuild.
uv run build --clean

# Flash the newest build artifact over J-Link.
uv run flash

# Flash a specific artifact.
uv run flash build/zephyr/zephyr.elf

# Verify the USB CDC ACM debug and command ports and their expected markers.
uv run smoke-test

# Smoke-test helpers.
uv run smoke-test --list-ports
uv run smoke-test --port COM7

# Lint project-owned source files with clang-tidy.
uv run lint

# Run host-compiled unit tests.
uv run unit-tests
```

Notes:

- `uv run build` is the default build entrypoint for this repo. It uses
  incremental builds (`--pristine auto`) for fast edit-compile cycles.
  Pass `--clean` to force a full rebuild (deletes `build/` and runs
  `--pristine always`). The script also refreshes the root
  `compile_commands.json`.
- `uv run flash` uses SEGGER J-Link by default and resolves the newest
  supported artifact under `build/zephyr`.
- `uv run smoke-test` discovers the Prism Kit CDC ACM ports and, by
  default, waits for `Task app_hw runtime:` and `Task app_main runtime:`
  markers on the debug channel before running a loopback test on the command
  channel. The command port is resolved as the other CDC ACM port.

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
  `bal::RunBootstrap(app::AppTask::SetupTrampoline,
  app::AppTask::LoopTrampoline)`.
- `bal/src/bootstrap.cpp` confirms OSHAL startup succeeded, initializes BAL
  resources, and creates the `app_main` task.
- `app/src/blink_app.cpp` owns the `AppTask` singleton. During `Setup()` it
  initializes Prism (logical strip, status LED, command port, controller),
  then `Loop()` fills the logical strip with the next color, calls `show()`,
  and sleeps.
- `app/src/prism_hw_backend.cpp` implements `prism::Initialize()` — it wires
  the BAL strip and status LED into the `app::hw` layer and starts the
  `app_hw` executor.
- `app/src/app_hw.cpp` starts the `app_hw` task that drains committed frames,
  writes them into the BAL strip, flushes the physical LEDs, services the
  command-port protocol, and toggles the board status LED.

This keeps APP free of Zephyr headers and devicetree details while preserving a
clear boot chain from Zephyr to OSHAL to BAL to APP.

### HW Backend

The HW backend (`app/src/`) owns the translation from logical Prism Kit
strip operations to physical WS2812 mutations. It is organised around five
classes inside `app::hw` (`app/src/hw/`):

```
┌─────────────────────────────────┐    ┌─────────────────────────────────┐
│  prism_hw_backend.cpp           │    │  HwTask (hw_task.cpp)           │
│  (APP task — producer)          │    │  (app_hw task — consumer)       │
│                                 │    │                                 │
│  prism::Initialize()            │    │  HwTask::Loop()                 │
│    → wires StripManager.Config  │    │    → event_group_.WaitAny()     │
│    → wires StatusLed.Configure  │    │    → StripManager::Apply()      │
│    → wires CommandManager       │    │    → CommandManager::Service()  │
│    → app::hw::StartHwExecutor() │    │    → StatusLed::Blink()         │
│                                 │    │                                 │
│  prism::Strip::show()           │    │  StripLedView (via StripManager)│
│    → StripLedView::SetColor()   │    │    → delegates to StripManager  │
│    → commits via mailbox        │    │    → applies to bal::Ws2812Strip│
└─────────────────────────────────┘    └─────────────────────────────────┘
```

| Class | Header | Responsibility |
|-------|--------|---------------|
| `HwTask` | `hw_task.hpp` | Process-wide singleton owning the app_hw task, its wake event-flag group, the strip manager, the command manager, and the status LED. `Start()` creates the Zephyr task; `Loop()` waits on event bits and dispatches to sub-managers. |
| `StripManager` | `strip_manager.hpp` | Owns the lock-free SPSC mailbox (`oshal::EventMailbox<SharedFrame>`) and the `StripLedView` array. `Configure()` wires the BAL strip and event group; `Apply()` polls the mailbox and writes committed frames to the physical strip. |
| `StripLedView` | `strip_manager.hpp` | Concrete `prism::StripLed` implementation. Each view stores a zero-based pixel index and back-pointer to the owning `StripManager`; all `SetColor()` calls are forwarded to the manager's staging buffer. |
| `CommandManager` | `command_manager.hpp` | Owns the command-port serial transport and the protocol adapter. Posts `kCommandRxEventMask` to the task event group on UART data arrival. |
| `StatusLed` | `status_led.hpp` | Manages the board status LED blink pattern. `Blink()` counts idle ticks and toggles the hardware at a fixed 0.5 Hz rate. |
| `HwCoordinator` | `hw_coordinator.hpp` | Free function `StartHwExecutor()` — idempotent entry point that starts the app_hw task. |

`SharedFrame` (in `shared_frame.hpp`) remains a POD struct — it is the data
payload, not behaviour, with a `kSharedFrameCapacity` of 16 pixels.

Controller commands arrive via `ControllerCommandSink` (in
`controller_command_sink.hpp`), which posts command bytes into the same
protocol pipeline that `CommandManager` owns.

## Setup/configuration Info

### Prerequisites

- Initialize submodules before building:

```bash
git submodule update --init --recursive
```

- Set up the Python toolchain. The repo uses `uv` for script execution and
  `pyproject.toml` for dependency management. Install the project and its
  dependencies:

```bash
uv sync
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

## Linting

The project uses **clang-tidy** with Google C++ Style naming rules enforced via
the `readability-identifier-naming` check.  Configuration lives in
[`.clang-tidy`](.clang-tidy).

### Prerequisites

- [LLVM/Clang](https://llvm.org) (clang-tidy is included in the LLVM
  distribution).
- The ARM GCC toolchain on PATH (so the lint script can resolve the C++ standard
  library headers).
- An existing build directory (run `uv run build` first) — the lint script
  reads `build/compile_commands.json` for compilation flags.

### Usage

```bash
# Lint all project-owned source files.
uv run lint

# Lint a single file.
uv run lint app/src/hw/strip_manager.cpp

# Apply auto-fixes (where supported).
uv run lint --fix

# List all enabled clang-tidy checks.
uv run lint --list-checks

# Pass extra flags to clang-tidy.
uv run lint --clang-tidy-extra='--extra-arg=-DZEPHYR_LOG_MODULE_NAME=mymod'
```

The script filters out cross-compiler flags that host clang-tidy cannot
process (`-mthumb`, `-mfp16-format=ieee`, `-fno-reorder-functions`, etc.) and
injects the ARM GCC C++ include paths so standard headers like `<array>` and
`<cstdint>` resolve correctly.

### Naming Rules

The `.clang-tidy` configuration enforces these Google C++ Style conventions:

| Category              | Convention        | Example                    |
|-----------------------|-------------------|----------------------------|
| Classes / Structs     | PascalCase        | `StripManager`, `RgbColor` |
| Functions / Methods   | PascalCase        | `IsReady()`, `SetColor()`  |
| Free functions        | PascalCase        | `Initialize()`, `Strip()`  |
| Member variables      | snake_case + `_`  | `led_count_`, `event_group_` |
| Constants             | `k` + PascalCase  | `kFrameEventMask`          |
| Enumerator values     | `k` + PascalCase  | `kPureRed`, `kSetMultipleColor` |
| Enums / Type aliases  | PascalCase        | `Color`, `InstructionTag`  |
| Namespaces            | snake_case        | `app::hw`, `oshal::internal` |
| Parameters / locals   | snake_case        | `led`, `color`, `set_ret`  |

Most naming violations are warnings (not errors) so the codebase can be
cleaned up incrementally.  Set `WarningsAsErrors: '*'` in `.clang-tidy`
to treat all violations as errors once the migration is complete.