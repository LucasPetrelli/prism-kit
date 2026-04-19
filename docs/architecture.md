# Architecture Notes

This document complements the repository README with a tighter view of the
layering rules and the boot path that phase 1 implements.

## Layer Ownership

### OSHAL

OSHAL owns the Zephyr-facing boundary. In phase 1 it provides:

- a shared status-code contract,
- a system readiness contract,
- a startup handoff hook contract,
- a C++ task-execution contract layered over Zephyr threads,
- a GPIO pin contract for SAMD21 pin PA17,
- a PWM output contract for SAMD21 pin PA8 routed as TCC0/WO[0],
- and the time/sleep contract APP currently needs.

The backend is currently Zephyr-based because that is the lowest-friction way
to validate architecture before introducing a timing-specific WS2812 engine.
The task interface is intentionally C++-only because no current C-shaped
integration point needs to create or manage tasks directly.

### BAL

BAL owns board resources and the application bootstrap. The Zephyr root entry
still hands off to BAL instead of calling APP directly, but now does so through
an OSHAL-declared handoff hook that the repository C++ composition layer
implements.

### APP

APP must not include Zephyr headers or know devicetree aliases. It talks to BAL
for board objects and OSHAL for execution services.

## Boot Flow

```text
Zephyr startup
    |
        +--> OSHAL SYS_INIT at APPLICATION level
            |
            +--> validate hardware-facing prerequisites
    |
        +--> main() in oshal/src/zephyr_system.c
            |
            +--> OSHAL-declared handoff hook
                |
                +--> C++ composition bridge
                    |
                    +--> BAL bootstrap with APP task entry callback
                    |
                        +--> initialize board LED object(s)
                        +--> launch APP task through OSHAL
                                |
                                +--> Zephyr schedules blink smoke test
```

## Why OSHAL Uses APPLICATION-Level SYS_INIT

The repository wants a staged boot path, but also needs Zephyr's device model to
have finished bringing up GPIO drivers first. `APPLICATION` is the best fit for
that compromise:

- it still runs before `main()`,
- it avoids racing driver initialization,
- and it keeps the OSHAL stage explicit in the final init sequence.

If a future WS2812 backend genuinely needs earlier hardware setup, that work can
move to a lower init level without changing the APP contract.

## J-Link Note

The upstream `seeeduino_xiao` board definition does not currently advertise a
Zephyr `jlink` runner. The recommended follow-up options are:

1. Add an out-of-tree board override that extends the upstream board with
   `jlink` runner support.
2. Keep the board target upstream and document a manual J-Link flow.

Neither choice requires changes to APP, BAL, or OSHAL.

## Build Shape

The repository still builds as a normal Zephyr application, but BAL and OSHAL
now compile as their own static libraries and then link into Zephyr's `app`
target.

- `ws2812_oshal` owns the Zephyr and SAMD21-facing implementation files and is
    the only layer target that links directly against `zephyr_interface`.
- `ws2812_bal` owns board policy and links against `ws2812_oshal`.
- Zephyr's root `app` target owns product sources plus the thin
    `src/boot_handoff_zephyr.cpp` composition file and links against both lower
    layer libraries.

That split matches the longer-term goal of extracting `oshal/` and `bal/` into
reusable submodules without pretending they are already independent repositories.