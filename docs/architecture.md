# Architecture Notes

This document complements the repository README with a tighter view of the
layering rules and the boot path that phase 2 implements.

## Layer Ownership

### OSHAL

OSHAL owns the Zephyr-facing boundary. In phase 2 it provides:

- a shared status-code contract,
- a system readiness contract,
- a startup handoff hook contract,
- a C++ task-execution contract layered over Zephyr threads,
- generic GPIO, PWM, and WS2812 transport interface contracts,
- a SAMD21-specific resource surface that publishes the concrete PA17 GPIO and
    PA8 TCC0/WO[0] PWM-plus-transport bindings for this build,
- and the time/sleep contract APP currently needs.

The backend is currently Zephyr-based and uses the SAMD21 PWM-plus-DMA path as
the first timing-specific WS2812 engine while keeping the public transport
contract independent from PWM versus future SPI signaling.
The task interface is intentionally C++-only because no current C-shaped
integration point needs to create or manage tasks directly.

### BAL

BAL owns board resources and the application bootstrap. The Zephyr root entry
still hands off to BAL instead of calling APP directly, but now does so through
an OSHAL-declared handoff hook that the repository C++ composition layer
implements. BAL also owns the board pin map that labels the physical SAMD21
resources for the XIAO wiring, the fixed 7-pixel strip object, logical RGB
pixel views, and the board-specific GRB ordering policy for that strip.

### APP

APP must not include Zephyr headers or know devicetree aliases. It talks to BAL
for board objects and OSHAL for execution services. APP does not talk directly
to PWM or any other transport backend.

## Boot Flow

```text
Zephyr startup
    |
        +--> OSHAL SYS_INIT at APPLICATION level
            |
            +--> validate physical hardware prerequisites
    |
        +--> main() in oshal/src/zephyr_system.c
            |
            +--> OSHAL-declared handoff hook
                |
                +--> C++ composition bridge
                    |
                    +--> BAL bootstrap with APP task entry callback
                    |
                        +--> map physical resources onto board-owned labels
                        +--> initialize board pin-map consumers
                        +--> initialize board LED object(s)
                        +--> initialize WS2812 strip object(s)
                        +--> launch APP task through OSHAL
                                |
                            +--> Zephyr schedules strip color-cycle demo
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
now compile as their own object libraries and then link into Zephyr's `app`
target.

- `oshal_interface` owns the Zephyr compile environment and hides the
    direct `zephyr_interface` dependency behind an OSHAL-named target.
- `oshal_implementation` owns the Zephyr and SAMD21-facing implementation
    files and re-exports that interface target publicly.
- `bal_implementation` owns board policy and links against
    `oshal_implementation`.
- Zephyr's root `app` target owns product sources plus the thin
    `src/boot_handoff_zephyr.cpp` composition file and links against both lower
    layer libraries.

That split matches the longer-term goal of extracting `oshal/` and `bal/` into
reusable submodules without pretending they are already independent repositories.