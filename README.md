# WS2812 SAMD21 LED Driver

This repository is a Zephyr-first firmware workspace for developing a WS2812
driver on the Seeed XIAO SAMD21. The current implementation is intentionally
small: it establishes the repo shape, keeps the application independent from
Zephyr and board details, and validates the boot path with a board LED blink
smoke test before any timing-critical WS2812 signaling is added.

## Current Status

Phase 1 is implemented.

- The repository is wired as a Zephyr application with a reproducible `west.yml`.
- Zephyr C++ support is enabled, and APP now builds as C++17 behind a
	C-compatible entry point.
- BAL and OSAL now also build as C++ where the code is policy-oriented rather
	than Zephyr-boundary-oriented.
- C++-first layers expose `.hpp` wrappers, while `.h` headers remain as the
	thin C ABI used by Zephyr-facing code.
- Code is split into HAL, BAL, OSAL, and APP layers with public headers.
- HAL startup runs through `SYS_INIT()` so the staged boot sequence remains
	explicit under Zephyr.
- BAL owns board resources and bootstraps the application.
- APP depends only on BAL and OSAL interfaces and currently blinks the board's
	`led0` alias as a smoke test.

## Design Intent

The repo is structured around four layers.

- `hal/`: hardware-facing services and early initialization.
- `bal/`: board abstraction and ownership of board resources such as status LEDs.
- `osal/`: the smallest portable operating-system facade the app currently needs.
- `app/`: product logic that should not care about Zephyr, SAMD21 registers, or
	board-specific pin names.

The dependency direction is one-way.

- APP may use BAL and OSAL.
- BAL may use HAL and call APP.
- HAL never depends on BAL or APP.
- Zephyr headers stay in the implementation files for HAL, BAL glue, OSAL, and
	the thin root entry point.

## Boot Sequence

The desired staged boot is kept, but adapted to Zephyr's lifecycle instead of
replacing it.

1. Zephyr boots and initializes the kernel and device model.
2. HAL runs a `SYS_INIT()` hook at the `APPLICATION` init level.
3. Zephyr enters `main()` in `src/main.c`.
4. `main()` transfers control to BAL.
5. BAL initializes board-owned objects and then calls APP.
6. APP runs using only BAL and OSAL interfaces.

This keeps the stage boundaries explicit without fighting Zephyr's startup
rules.

## Repository Layout

```text
.
|-- app/
|   |-- CMakeLists.txt
|   |-- include/app/app.h
|   |-- include/app/app.hpp
|   `-- src/blink_app.cpp
|-- bal/
|   |-- CMakeLists.txt
|   |-- include/bal/bootstrap.h
|   |-- include/bal/bootstrap.hpp
|   |-- include/bal/led.h
|   |-- include/bal/led.hpp
|   `-- src/
|       |-- bootstrap.cpp
|       `-- board_led.cpp
|-- docs/
|   `-- architecture.md
|-- hal/
|   |-- CMakeLists.txt
|   |-- include/hal/gpio.h
|   |-- include/hal/system.h
|   `-- src/
|       |-- gpio_zephyr.c
|       `-- system_zephyr.c
|-- osal/
|   |-- CMakeLists.txt
|   |-- include/osal/time.h
|   |-- include/osal/time.hpp
|   `-- src/time_zephyr.cpp
|-- src/
|   `-- main.c
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

### Board Target

The upstream Zephyr board target for the XIAO SAMD21 is `seeeduino_xiao`.

The current phase-1 implementation assumes that board target and uses the board
`led0` devicetree alias for the smoke test. The alias already captures whether
the LED is active-high or active-low, so the blink code does not hard-code the
polarity.

### Flash and Debug Strategy

There is one current tooling caveat: upstream `seeeduino_xiao` advertises
`bossac` and `openocd` runners, but not a Zephyr `jlink` runner.

That means the current repo supports two realistic paths.

1. Use upstream Zephyr runners for the earliest bring-up while the architecture
	 settles.
2. Use J-Link manually over SWD now, and add a local board override later if
	 you want `west flash` and `west debug` to drive J-Link directly.

The code structure in this repo is intentionally independent from that decision.

## Getting Started

These commands assume you start in the repository root.

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
is at `C:\dev\ws2812-samd21-led-driver`, then the west workspace topdir becomes
`C:\dev`, and Zephyr is fetched as a sibling directory such as `C:\dev\zephyr`.

That is normal, and it means the fetched dependencies stay outside the app repo
itself.

## Build

Build the current blink smoke test for the XIAO SAMD21 with a pristine build.

```bash
west build -b seeeduino_xiao --pristine always .
```

On Windows with the GNU Arm Embedded Toolchain installed under `Program Files
(x86)`, use the provided helper script instead. It resolves the toolchain to a
short path so the linker does not break on spaces in the install directory.

From Git Bash:

```bash
bash scripts/build-local.sh
```

From PowerShell:

```powershell
bash scripts/build-local.sh
```

The resulting ELF is expected at `build/zephyr/zephyr.elf`.
 
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

Start the GDB server:

```bash
JLinkGDBServerCLExe -device ATSAMD21G18A -if SWD -speed 4000 -singlerun
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
without touching APP, BAL, or HAL interfaces.

## Layer Walkthrough

### HAL

HAL currently exposes two small contracts.

- `hal/system.h`: reports whether early HAL startup succeeded.
- `hal/gpio.h`: exposes abstract GPIO signals rather than raw Zephyr GPIO
	types to upper layers.

The phase-1 HAL backend is Zephyr-based. That keeps startup simple now while
leaving room for a lower-level timing backend later for WS2812 bit generation.

### BAL

BAL owns board resources and the transition into the application.

- `bal/bootstrap.h`: one entry point that prepares the board layer and then
	calls APP.
- `bal/led.h`: a board-level LED object API that hides how the LED is wired.

Phase 1 exposes only a single status LED object because that is enough to test
the architecture.

### OSAL

OSAL stays narrow on purpose. The only service exposed today is millisecond
sleep. More services should be added only when the app actually needs them.

### APP

APP is intentionally unaware of Zephyr headers, devicetree aliases, and the
SAMD21 GPIO backend. It asks BAL for a status LED object and uses OSAL for
timing.

The current APP implementation is compiled as C++17, but its public entry point
stays C-compatible so BAL and the low-level Zephyr-facing stages can remain in C
until there is a stronger reason to migrate them.

BAL and OSAL now follow the same pattern in their implementations. HAL GPIO and
startup code stay in C because they sit directly on Zephyr's device and init
boundaries, where the C-shaped integration points are easier to inspect.

For layers that are primarily consumed from C++, the repository now provides a
paired header pattern:

- `.h` exposes the stable C ABI.
- `.hpp` exposes inline C++ wrappers that keep C++ call sites idiomatic without
	changing the exported firmware boundary.

## Smoke Test Behavior

The current application toggles the board-owned status LED forever.

- If HAL startup fails, BAL refuses to start APP.
- If BAL cannot initialize the board LED object, APP never starts.
- If the blink loop starts, the repository layering and boot path are working.

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

That decision can be added behind the current HAL and BAL seams without changing
the app contract.
