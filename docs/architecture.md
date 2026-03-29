# Architecture Notes

This document complements the repository README with a tighter view of the
layering rules and the boot path that phase 1 implements.

## Layer Ownership

### HAL

HAL owns hardware-facing services and early startup. In phase 1 it provides:

- a system readiness contract,
- and a GPIO signal contract for the board status LED.

The backend is currently Zephyr-based because that is the lowest-friction way
to validate architecture before introducing a timing-specific WS2812 engine.

### BAL

BAL owns board resources and the application bootstrap. That is why `main()`
hands off to BAL instead of calling APP directly.

### OSAL

OSAL wraps only what APP actually needs. Right now that is sleep/time. Thread,
queue, or mutex abstractions should be added only when the app needs them.

### APP

APP must not include Zephyr headers or know devicetree aliases. It talks to BAL
for board objects and OSAL for execution services.

## Boot Flow

```text
Zephyr startup
    |
    +--> HAL SYS_INIT at APPLICATION level
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

## Why HAL Uses APPLICATION-Level SYS_INIT

The repository wants a staged boot path, but also needs Zephyr's device model to
have finished bringing up GPIO drivers first. `APPLICATION` is the best fit for
that compromise:

- it still runs before `main()`,
- it avoids racing driver initialization,
- and it keeps the HAL stage explicit in the final init sequence.

If a future WS2812 backend genuinely needs earlier hardware setup, that work can
move to a lower init level without changing the APP contract.

## J-Link Note

The upstream `seeeduino_xiao` board definition does not currently advertise a
Zephyr `jlink` runner. The recommended follow-up options are:

1. Add an out-of-tree board override that extends the upstream board with
   `jlink` runner support.
2. Keep the board target upstream and document a manual J-Link flow.

Neither choice requires changes to APP, BAL, or OSAL.