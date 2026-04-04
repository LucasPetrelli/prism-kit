# Architecture Notes

This document complements the repository README with a tighter view of the
layering rules and the boot path that phase 1 implements.

## Layer Ownership

### OSHAL

OSHAL owns the Zephyr-facing boundary. In phase 1 it provides:

- a shared status-code contract,
- a system readiness contract,
- a GPIO pin contract for SAMD21 pin PA17,
- and the time/sleep contract APP currently needs.

The backend is currently Zephyr-based because that is the lowest-friction way
to validate architecture before introducing a timing-specific WS2812 engine.

### BAL

BAL owns board resources and the application bootstrap. That is why `main()`
hands off to BAL instead of calling APP directly.

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
    +--> main()
            |
            +--> BAL bootstrap
                    |
                    +--> initialize board LED object(s)
                    +--> call APP entry
                            |
                            +--> blink smoke test
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