# Prism Kit

This repository is a Zephyr-first firmware workspace for developing a WS2812
driver on the Seeed XIAO SAMD21. The current implementation keeps the
application independent from Zephyr and board details while driving a 7-pixel
WS2812 strip over a PWM-sequence-backed OSHAL transport.

## Current Status

Phase 2 is implemented.

- The repository is wired as a Zephyr application with a reproducible `west.yml`.
- Zephyr C++ support is enabled, and APP now builds as C++17 behind a
	C++ task entry point.
- BAL and OSHAL now build as dedicated object libraries where the code is
	policy-oriented rather than Zephyr-app-target-oriented, while the direct
	Zephyr init hook stays in C.
- APP and BAL public interfaces are now C++-only.
- OSHAL keeps `.h` headers only where a real C-facing Zephyr boundary still
	exists.
- OSHAL tasking is intentionally C++-only and is exposed through
	`oshal/task.hpp` because the current repo has no C-shaped consumer for that
	service.
- Code is split into OSHAL, BAL, and APP layers with public headers.
- OSHAL startup runs through `SYS_INIT()` so the staged boot sequence remains
	explicit under Zephyr.
- `main()` still lives in OSHAL, but the APP-plus-BAL startup composition now
	lives in a thin repository-level handoff file so `zephyr_system.c` no longer
	includes APP or BAL headers.
- BAL owns board resources and bootstraps the application from a supplied APP
	task entry point.
- OSHAL now keeps generic GPIO, PWM, and WS2812 interfaces separate from the
	SAMD21-specific resource exports used by this build.
- BAL now owns a 7-pixel WS2812 strip object and logical RGB pixel views.
- BAL now owns the board pin map that labels the SAMD21 physical resources for
	the XIAO wiring.
- APP depends only on BAL and OSHAL interfaces and now cycles the full strip
	through red, blue, and green while still blinking the board status LED on
	PA17.
- OSHAL and BAL are now mounted into this repository as dedicated Git
	submodules at `oshal/` and `bal/` while preserving the existing layer
	contracts and include paths.

## Design Intent

The repo is structured around three layers.

- `oshal/`: the single Zephyr-boundary layer for GPIO, sleep/time, C++ task
	execution, early initialization, and transport backends such as WS2812 frame
	output, plus shared status codes.
- `bal/`: board abstraction and ownership of board resources such as status
	LEDs and the 7-pixel WS2812 strip.
- `app/`: product logic that should not care about Zephyr, SAMD21 registers, or
	board-specific pin names.
- `src/`: thin repository-level C++ composition glue that satisfies
	OSHAL-declared handoff hooks without moving `main()` out of OSHAL.

`oshal/` and `bal/` now live as reusable submodules mounted at stable in-tree
paths so the Zephyr application can keep the same include graph and build
shape.

- `oshal/` is intended to carry the reusable Zephyr and SAMD21-facing runtime
	contracts and backends.
- `bal/` is intended to carry reusable Seeed XIAO board ownership and policy.
- `app/` remains the product-specific layer for this repository.

The dependency direction is one-way.

- APP may use BAL and OSHAL.
- BAL may use OSHAL and invoke a supplied APP entry point, but does not depend
	on APP headers directly.
- OSHAL interfaces never depend on BAL or APP.
- Zephyr headers stay in the implementation files for OSHAL, BAL glue, and the
	thin composition glue hosted in `src/boot_handoff_zephyr.cpp`.

## Boot Sequence

The desired staged boot is kept, but adapted to Zephyr's lifecycle instead of
replacing it.

1. Zephyr boots and initializes the kernel and device model.
2. OSHAL runs a `SYS_INIT()` hook at the `APPLICATION` init level.
3. Zephyr enters `main()` in `oshal/src/zephyr_system.c`.
4. `main()` calls an OSHAL-declared handoff hook implemented by the repository composition layer.
5. BAL initializes board-owned objects and then launches APP as an OSHAL task.
6. Zephyr schedules APP after startup, and APP runs using only BAL and OSHAL interfaces.

This keeps the stage boundaries explicit without fighting Zephyr's startup
rules.

## Repository Layout

```text
.
|-- app/
|   |-- CMakeLists.txt
|   |-- include/app/app.hpp
|   `-- src/blink_app.cpp
|-- bal/
|   |-- CMakeLists.txt
|   |-- include/bal/bootstrap.hpp
|   |-- include/bal/led.hpp
|   `-- src/
|       |-- bootstrap.cpp
|       |-- led.cpp
|       |-- led_internal.hpp
|       |-- pin_map.hpp
|       |-- ws2812.cpp
|       |-- ws2812_internal.hpp
|       `-- seeeduino_xiao/
|           |-- led.cpp
|           |-- pin_map.cpp
|           `-- ws2812.cpp
|-- docs/
|   `-- architecture.md
|-- oshal/
|   |-- CMakeLists.txt
|   |-- include/oshal/debug_port.hpp
|   |-- include/oshal/gpio.hpp
|   |-- include/oshal/pwm.hpp
|   |-- include/oshal/samd21_resources.hpp
|   |-- include/oshal/status.h
|   |-- include/oshal/system.h
|   |-- include/oshal/task.hpp
|   |-- include/oshal/time.h
|   |-- include/oshal/time.hpp
|   `-- src/
|       |-- samd21_pwm.cpp
|       |-- zephyr_debug_port.cpp
|       |-- zephyr_debug_port_cdc_acm.cpp
|       |-- zephyr_gpio.cpp
|       |-- samd21.cpp
|       |-- zephyr_system.c
|       `-- zephyr_time.cpp
|-- src/
|   `-- boot_handoff_zephyr.cpp
|-- CMakeLists.txt
|-- prj.conf
`-- west.yml
```

## Tooling

### Zephyr and West

Zephyr is the platform and build owner. `west` is the workspace manager that
pulls Zephyr and its modules, while CMake is the build backend used under the
hood by `west build`.

The manifest in this repository pins the Zephyr revision and then imports the
upstream Zephyr manifest for compatibility. That keeps setup reliable while this
repo is still in early bring-up.

### Code Formatting

The repository now carries a root `.clang-format` file derived from `Google`
style with explicit two-space indentation and spaces instead of literal tabs.

The repository also carries checked-in VS Code settings that enable
format-on-save for C and C++ files when you have a clang-format-capable
formatter extension installed.

If `clang-format` is on your `PATH`, you can format the tracked firmware source
tree from the repository root with:

```bash
python scripts/format.py
```

To check formatting in CI or before committing without rewriting files:

```bash
python scripts/format.py --check
```

You can also target specific files or folders:

```bash
python scripts/format.py app/src/blink_app.cpp oshal/include
```

On Windows, the helper also falls back to
`C:\Program Files\LLVM\bin\clang-format.exe` if it is installed there.

To format only staged C and C++ files from the current git index:

```bash
python scripts/format.py --staged
```

To install the repo-local pre-commit hook that formats staged C and C++ files
and re-stages the rewritten results before each commit:

```bash
python scripts/install_git_hooks.py
```

That installer sets `core.hooksPath` to the tracked `.githooks` directory for
this clone. The hook itself runs:

```bash
python scripts/format.py --staged --git-add
```

If you prefer to wire the hook path yourself, the equivalent manual command is:

```bash
git config core.hooksPath .githooks
```

### Board Target

The upstream Zephyr board target for the XIAO SAMD21 is `seeeduino_xiao`.

The current implementation assumes that board target. OSHAL publishes the
generic GPIO, PWM, and WS2812 contracts in its agnostic headers, and publishes
the concrete SAMD21 PA17 and PA8 TCC0/WO[0] resources through
`oshal/samd21_resources.hpp`. BAL then maps the board status LED and WS2812
strip policy onto those physical resources and owns the XIAO-specific active-low
and GRB decisions.

### Flash and Debug Strategy

There is one current tooling caveat: upstream `seeeduino_xiao` advertises
`bossac` and `openocd` runners, but not a Zephyr `jlink` runner.

That means the current repo supports two realistic paths.

1. Use upstream Zephyr runners for the earliest bring-up while the architecture
	 settles.
2. Use J-Link manually over SWD now, and add a local board override later if
	 you want `west flash` and `west debug` to drive J-Link directly.

The code structure in this repo is intentionally independent from that decision.

The repository also carries project-local helper scripts that fit the shared
Copilot skill contract.

- `python scripts/build.py` builds the firmware from the project root.
- `python scripts/flash.py` flashes the newest Zephyr artifact with J-Link by
	default, or a user-supplied artifact path.
- `python scripts/smoke_test.py` waits for the USB CDC ACM debug console and
	verifies that the firmware is still emitting the expected runtime markers.

## Getting Started

These commands assume you start in the repository root.

### 0. Initialize the BAL and OSHAL submodules

For a fresh clone, prefer:

```bash
git clone --recurse-submodules <repo-url>
```

For an existing clone, run:

```bash
git submodule update --init --recursive
```

### 1. Create and activate a Python environment

Windows PowerShell:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
```

Git Bash on Windows:

```bash
python -m venv .venv
source .venv/Scripts/activate
```

### 2. Install west

```bash
pip install --upgrade pip west jsonschema pyelftools
```

To use the CDC ACM smoke-test helper, also install `pyserial` in the same
environment:

```bash
pip install pyserial
```

### 3. Smoke-test the debug console

After flashing the firmware, you can verify that the USB CDC ACM debug UART is
alive with:

```bash
python scripts/smoke_test.py
```

The default smoke test waits for the port whose USB product string matches
`Prism Kit Debug Console`, then requires at least one
`Task app_main runtime:` line before passing. If you start it immediately after
flashing or resetting the board, it will usually also observe the Zephyr boot
banner and the `DebugPort online ...` startup line.

Useful overrides:

- `python scripts/smoke_test.py --list-ports`
- `python scripts/smoke_test.py --port COM7`
- `python scripts/smoke_test.py --require "DebugPort online on"`
- `python scripts/smoke_test.py --no-default-requirements --require "*** Booting Zephyr OS build"`

The extra Python packages are required by Zephyr's board discovery and ELF
introspection scripts during configuration and build.

### 3. Initialize the workspace from this repository's manifest

This flow assumes the repository is a normal Git checkout. If this directory is
just a plain folder with no `.git` metadata yet, `west init -l .` may create the
workspace metadata but later manifest operations can still fail. In that case,
either initialize Git for this repo first or create a separate Zephyr workspace
and point `west build` at this app by path.

```bash
west init -l .
west update
west zephyr-export
```

When you run `west init -l .` from inside an already cloned repository, west
creates the workspace topdir in the parent directory. For example, if this repo
is at `C:\dev\prism-kit`, then the west workspace topdir becomes
`C:\dev`, and Zephyr is fetched as a sibling directory such as `C:\dev\zephyr`.

That is normal, and it means the fetched dependencies stay outside the app repo
itself.

## Build

Build the current demo firmware for the XIAO SAMD21 with a pristine build.

```bash
west build -b seeeduino_xiao --pristine always .
```

On Windows with the GNU Arm Embedded Toolchain installed under `Program Files
(x86)`, use the provided helper script instead. It resolves the toolchain to a
short path so the linker does not break on spaces in the install directory.

With the repository virtual environment activated:

```bash
python scripts/build.py
```

Without activating the virtual environment first:

```text
.venv\Scripts\python.exe scripts\build.py
```

The resulting ELF is expected at `build/zephyr/zephyr.elf`.

Internally, the build now compiles `oshal/` and `bal/` as their own static
libraries and links them into Zephyr's `app` target. That keeps the current
firmware build Zephyr-native while consuming BAL and OSHAL through their
submodule mount points.
 
For clangd-based editor indexing, the build helper also refreshes
`compile_commands.json` at the repository root from the Zephyr build directory.
The tracked `.clangd` file points clangd at `build/` as the compilation
database location.

## Flash

### Upstream Runner Path

If you want the most direct path with upstream board support as-is:

```bash
west flash
```

For the `seeeduino_xiao` board this generally follows the USB bootloader path.
You may need to double-tap reset to enter bootloader mode before flashing.

### Manual J-Link Path

If you want to use J-Link immediately, use the SWD pads on the XIAO and drive
the session manually. The current repo does not yet add a local Zephyr board
override for a `jlink` runner.

Flash the newest supported build artifact in one line from the repository root.

With the virtual environment activated:

```text
python scripts/flash.py
```

Without activating the virtual environment first:

```text
.venv\Scripts\python.exe scripts\flash.py
```

The helper prefers the newest `.elf`, then `.hex`, then `.bin` under
`build/zephyr`. If it has to flash a raw `.bin`, it reads the flash base and
load offset from `build/zephyr/.config` so the write address stays aligned with
the Zephyr build. It also searches versioned SEGGER install directories such as
`C:\Program Files\SEGGER\JLink_V*\JLink.exe`, which was the missing case in
the earlier helper. You can also pass an explicit artifact path as the first
argument.

Explicit artifact example:

```text
python scripts/flash.py build/zephyr/zephyr.elf
```

Dry-run example to verify tool and artifact discovery without touching hardware:

```text
python scripts/flash.py --dry-run
```

If you want to debug instead of doing a one-shot flash, start the GDB server:

```bash
JLinkGDBServerCL.exe -device ATSAMD21G18A -if SWD -speed 4000 -singlerun
```

Then load the firmware with GDB:

```bash
arm-none-eabi-gdb build/zephyr/zephyr.elf \
	-ex "target remote localhost:2331" \
	-ex "monitor reset" \
	-ex "load" \
	-ex "monitor reset" \
	-ex "continue"
```

The `ATSAMD21G18A` device name is the expected J-Link target string for this
board family, but you should verify it against the SEGGER device database on
the host you actually use.

## Debug

With the manual J-Link path above, you can also stop before `continue` and use
the same GDB session for step-debugging.

If you prefer to stay fully inside the Zephyr runner model, the repo is already
organized so that a future out-of-tree board override can add `jlink` support
without touching APP, BAL, or OSHAL interfaces.

## Layer Walkthrough

### OSHAL

OSHAL currently exposes five small contracts.

- `oshal/status.h`: defines the project-wide status codes shared across layers.
- `oshal/system.h`: reports whether early OSHAL startup succeeded.
- `oshal/gpio.hpp`: exposes the generic C++ GPIO interface without leaking
	Zephyr GPIO types upward.
- `oshal/pwm.hpp`: exposes the generic C++ PWM interfaces without leaking
	SAMD21 timer details upward.
- `oshal/samd21_resources.hpp`: publishes the concrete SAMD21 physical resource
	handles used by this build.
- `oshal/time.h`: exposes millisecond sleep without leaking Zephyr kernel APIs.

The phase-1 OSHAL backend is Zephyr-based. That keeps startup simple now while
leaving room for a lower-level timing backend later for WS2812 bit generation.

### BAL

BAL owns board resources and the transition into the application.

- `bal/bootstrap.hpp`: one entry point that prepares the board layer and then
	launches a caller-supplied APP task entry point.
- `bal/led.hpp`: a board-level LED object API that hides how the LED is wired.
- `bal/src/pin_map.hpp`: an internal board pin map that labels physical OSHAL
	resources with board-meaningful roles.

The current BAL surface exposes one board-owned status LED object and one fixed
7-pixel WS2812 strip object, which is enough to exercise the ownership and
transport boundaries.

### APP

APP is intentionally unaware of Zephyr headers, devicetree aliases, and the
SAMD21 GPIO backend. It asks BAL for a status LED object and uses OSHAL for
timing.

The current APP implementation is compiled as C++17 and now exposes a C++ task
entry directly because the repository-level handoff moved into a thin C++
composition file.

BAL and the higher-level OSHAL wrappers now expose C++ interfaces directly.
The direct OSHAL startup hook stays in C because it sits on Zephyr's
`SYS_INIT()` boundary, and the same translation unit still hosts `main()`. The
APP-plus-BAL composition handoff now lives in `src/boot_handoff_zephyr.cpp`
with only a thin `extern "C"` bridge for `oshal_main_handoff()`, so OSHAL still
avoids direct APP or BAL header dependencies. The GPIO and PWM backends
themselves remain implemented in C++ behind board-owned OSHAL objects.

The repository build also treats BAL and OSHAL as independent object libraries
so their public headers, private implementation files, and inter-layer
dependency rules line up with the current submodule split.
For layers that cross a real C boundary, the repository provides narrow `.h`
headers alongside the C++ interface when needed:

- `.h` exposes the stable C ABI.
- `.hpp` exposes inline C++ wrappers that keep C++ call sites idiomatic without
	changing the exported firmware boundary.

APP, BAL, and OSHAL tasking are now C++-only by design. The remaining `.h`
headers are the OSHAL contracts that still cross the direct Zephyr-facing C
startup boundary.

## Smoke Test Behavior

The current application cycles the board-owned WS2812 strip through red, blue,
and green while toggling the board-owned status LED once per color step.

- If OSHAL startup fails, BAL refuses to start APP.
- If BAL cannot initialize the board LED object, APP never starts.
- If BAL cannot initialize the board WS2812 strip object, APP never starts.
- If the color-cycle loop starts, the repository layering and boot path are
	working.

This is the right first milestone before implementing WS2812 signaling, because
it validates structure, startup, build wiring, and debug access in isolation.

## Architecture Notes

More detail on the layering rules and boot ownership lives in
`docs/architecture.md`.

## Next Steps

The next implementation slice should decide the WS2812 signaling backend.

Reasonable candidates are:

- a carefully timed GPIO backend,
- a timer-assisted backend,
- or an encoded SPI/SERCOM backend if it maps cleanly to the SAMD21.

That decision can be added behind the current OSHAL and BAL seams without changing
the app contract.
