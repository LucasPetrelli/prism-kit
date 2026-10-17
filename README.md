# Prism Kit

## Purpose

Prism Kit is an embedded firmware project that constructs an RGB LED controller
from two reusable layers:

- `oshal/` provides hardware and operating-system services.
- `bal/` provides board-level resources and policy.
- `app/` contains the product behavior and controller workflow.

The project keeps product code independent from MCU, board, and RTOS details.

## Architecture

Dependencies point from the product toward the platform:

```text
app -> bal -> oshal -> Zephyr and platform backends
```

- **OSHAL** owns the hardware and OS boundary: startup, tasks, timing,
  synchronization, communication, and hardware primitives. The integrating
  project owns task stack storage and supplies it through OSHAL's task API.
- **BAL** owns board resources and board policy: LEDs, sensors, transports,
  board configuration, and the handoff into the application.
- **APP** owns the RGB LED controller, protocols, user-facing behavior, and
  backend composition.

Startup follows the same ownership direction: platform startup enters OSHAL,
OSHAL hands off to BAL, and BAL launches the application.

## Repository Layout

- `app/` - product behavior and application backends.
- `bal/` - board abstraction layer; see [`bal/README.md`](bal/README.md).
- `oshal/` - hardware and OS abstraction layer; see
  [`oshal/README.md`](oshal/README.md).
- `include/` and `protocol/` - project-level public contracts.
- `scripts/` and `tests/` - project tooling and verification.

## Development

Run commands from `prism-kit/`.

On the first build, choose a project board explicitly. The wrapper saves the
validated alias in the ignored, root-level `.prism-kit.local.json`; later builds
reuse that choice, while another explicit `--board` selection replaces it.
Choose `seeeduino_xiao` for the SAMD21 or `xiao_nrf52_sense` for the staged
XIAO nRF52840 Sense target. The remembered file is outside `build/` and survives
`--clean`. Direct West/CMake callers must still select the Zephyr board target
explicitly.

```bash
git submodule update --init --recursive
uv sync
uv run python scripts/install_git_hooks.py
uv run build --board seeeduino_xiao  # first use; saves the selected board
uv run build                         # later uses the saved choice
uv run build --board xiao_nrf52_sense --oshal-demo blinky
uv run build --board seeeduino_xiao --oshal-demo samd21-dma-pwm
uv run unit-tests
uv run format --check
uv run lint-python
uv run lint
```

Use `uv run flash` and `uv run smoke-test` for hardware validation when the
required target and probe are available. Project-owned wrappers define the
supported options for each command.

The hook installer configures the parent repository and the `bal/` and
`oshal/` submodules. Their pre-commit hooks check staged C/C++ files and, when
formatting is needed, apply it in the working tree and block the commit so the
changes can be reviewed and staged explicitly.

The standalone SAMD21 DMA/PWM demo submits a finite 26-period pulse train (a
25-value pulse-width ramp plus a zero-duty terminal period) about every 2 ms,
and omits the normal APP/BAL runtime composition. Build it with
`uv run build --oshal-demo samd21-dma-pwm`; a regular `uv run build` selects
`none` and restores the normal product image. Use `--clean` when switching
board targets in the shared build directory. The `uv run flash` wrapper selects
the artifact and J-Link device from a content-validated provenance record
written after a successful build. A missing or stale record fails closed; an
external artifact requires an explicit `--device`, and an external `.bin` also
requires `--load-address`.

The generic OSHAL `blinky` demo uses only OSHAL APIs and the selected board's
`gpio0` resource, and omits the BAL and APP runtime. Its selected-board
Devicetree requirements are checked by a Zephyr-side configuration source.
The blink loop runs as an OSHAL task named `blinky`; when the CDC ACM debug
port is ready, task lifecycle and 10-second runtime diagnostics are printed
there. This demo enables OSHAL task diagnostics directly; they do not depend
on Zephyr's `CONFIG_LOG` or the `--debug-opt` optimization option.
It is available on the XIAO nRF52840 Sense and the existing SAMD21 profile. On
a first build, pass the board explicitly, for example
`uv run build --board xiao_nrf52_sense --oshal-demo blinky`.

### Debug Traces

Trace output is sent through the debug port. APP trace variables are set in
`app/CMakeLists.txt` (or passed as `-D<NAME>=ON` when invoking CMake or `west`
directly). The OSHAL WS2812 frame trace is controlled separately through
`OSHAL_LOG_WS2812_FRAME` in `oshal/CMakeLists.txt`; it defaults to `ON` and can
be disabled with `-DOSHAL_LOG_WS2812_FRAME=OFF`. The per-pixel LED trace
defaults to `OFF` so it can be enabled without changing controller logging:

- `PRISM_KIT_LOG_EVB` - `[EVB]` event-mailbox send/receive traces.
- `PRISM_KIT_LOG_CONTROLLER` - `[Controller]` controller trace output.
- `PRISM_KIT_LOG_LED` - `[LED] apply` per-pixel records with strip name,
  zero-based index, logical `#RRGGBB`, and the `SetColor` status at the
  APP-to-BAL boundary.
- `OSHAL_LOG_WS2812_FRAME` - `[OSHAL][WS2812]` frame bytes before pulse
  encoding and the comma-separated decimal DMA pulse sequence (ns) before
  playback (ON by default; set to `OFF` to suppress both traces).

## Agent Guidance

Before changing code:

1. Identify the owning layer and read its README and public headers.
2. Keep dependencies moving in the direction `app -> bal -> oshal`.
3. Keep board and platform details behind the nearest abstraction boundary.
4. Prefer small changes with focused tests, then run the project wrapper that
   covers the changed layer.

Do not edit imported dependency trees such as `zephyr/`, `modules/`, or
`bootloader/` unless the change explicitly targets an upstream dependency.
