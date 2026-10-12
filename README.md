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

```bash
git submodule update --init --recursive
uv sync
uv run build
uv run build --oshal-demo SAMD21_DMA_PWM
uv run unit-tests
uv run lint
```

Use `uv run flash` and `uv run smoke-test` for hardware validation when the
required target and probe are available. Project-owned wrappers define the
supported options for each command.

The standalone SAMD21 DMA PWM demo submits a finite 26-period pulse train (a
25-value pulse-width ramp plus a zero-duty terminal period) about every 2 ms,
and omits the normal APP/BAL runtime composition. Build it with
`uv run build --oshal-demo SAMD21_DMA_PWM`; a regular `uv run build` explicitly
selects `NONE` and restores the normal product image. The existing
`uv run flash` wrapper can program whichever image was most recently built.

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
