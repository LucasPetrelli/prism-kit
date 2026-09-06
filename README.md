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
  synchronization, communication, and hardware primitives.
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
uv run unit-tests
uv run lint
```

Use `uv run flash` and `uv run smoke-test` for hardware validation when the
required target and probe are available. Project-owned wrappers define the
supported options for each command.

## Agent Guidance

Before changing code:

1. Identify the owning layer and read its README and public headers.
2. Keep dependencies moving in the direction `app -> bal -> oshal`.
3. Keep board and platform details behind the nearest abstraction boundary.
4. Prefer small changes with focused tests, then run the project wrapper that
   covers the changed layer.

Do not edit imported dependency trees such as `zephyr/`, `modules/`, or
`bootloader/` unless the change explicitly targets an upstream dependency.
