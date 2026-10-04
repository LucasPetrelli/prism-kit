# Fade Instruction and Rainbow4

[controller-api]: ../../../include/prism/controller.hpp
[instruction-api]: ../../../include/prism/instruction.hpp
[color-api]: ../../../include/prism/color.hpp
[strip-api]: ../../../include/prism/strip.hpp
[controller-src]: ../controller.cpp
[app-task]: ../../src/blink_app.cpp
[controller-command]: ../../src/hw/controller_command.hpp
[command-sink]: ../../src/hw/controller_command_sink.cpp
[protocol-api]: ../../../protocol/include/protocol.hpp
[protocol-tags]: ../../../protocol/include/tags.hpp
[controller-tests]: ../../../tests/controller_test.cpp
[command-tests]: ../../../tests/controller_command_test.cpp
[rainbow-palette]: ../../../scripts/rainbow2.py
[rainbow3-tests]: ../../../scripts/tests/test_rainbow3.py
[architecture]: ../../../docs/architecture.md

## Goal

Add a one-pixel Fade instruction that scales the pixel's current HSV Value over
a duration, while preserving its hue and saturation. Use it to create
`rainbow4`: initialize the seven-color palette, then give each LED an
independently repeating 2-second fade to 25% followed by a 2-second fade to
400%, staggered by approximately 2/7 second per LED.

The user confirmed:

- Fade percentages are multipliers of the V captured when that Fade starts.
  Thus 25% dims to one quarter of the current V; a following 400% Fade restores
  it, subject to the HSV maximum of 255.
- The seven per-LED child groups should repeat indefinitely.
- Unit tests come before the protocol/hardware integration and `rainbow4`.

## Codebase Fit and Constraints

This is APP behavior. `ControllerInstruction::Execute()` already supports
yielding for a positive delay, the controller schedules the earliest
instruction deadline or group mark, and the APP task re-arms the existing
one-shot timer [controller API][controller-api] [controller source][controller-src]
[APP task][app-task]. Fade should use this path and update the Prism strip
through its existing contract; it should not add OSHAL timers or direct BAL access
[architecture][architecture].

`StripLed::Color()` returns the staged logical RGB value, not measured physical
brightness [strip API][strip-api]. The Fade captures that value when it becomes
due. Existing HSV uses 8-bit H, S, and V values, with V in `0..255`; the current
conversion helpers are [HsvToRgb and RgbToHsv][color-api].

The current program and command capacities need coordinated changes:

- The instruction table holds 16 instructions
  [instruction API][instruction-api]. `rainbow4` needs 28: seven color sets,
  seven `RunGroup` calls, and fourteen Fades. Group count is instruction count
  plus the implicit root; the controller's 32-bit group mask limits instruction
  capacity to at most 31 without a graph representation change
  [controller API][controller-api] [controller source][controller-src].
- Child `RunGroup` forever repeats are explicitly rejected during preflight,
  although the runtime already handles the forever sentinel. The existing
  controller tests assert the rejection [controller source][controller-src]
  [controller tests][controller-tests]. Allowing these repeats must retain
  cycle rejection and positive-boundary validation.
- The command sink currently registers twelve controller commands plus the
  built-in loopback handler. The protocol handler table holds thirteen total
  [command sink][command-sink] [protocol API][protocol-api]
  [protocol tags][protocol-tags]. One group-aware Fade tag needs a capacity of
  fourteen.
- The HW-to-APP command mailbox holds twenty messages
  [controller command][controller-command]. `rainbow4` sends thirty messages
  in its program burst: reset, 28 instructions, and start. Increase the mailbox
  capacity to 32, or introduce reliable host pacing/backpressure. The current
  one-way protocol provides no acknowledgement.

## Proposed Behavior

### Fade Semantics

The Fade percentage is a multiplier, not a percentage-point delta. At
activation, capture the staged color and let `V0` be its HSV Value. Compute:

```text
targetV = min(255, round(V0 * percent / 100))
V(t)    = V0 + round((targetV - V0) * clamp(elapsed / duration, 0, 1))
```

Interpolate using elapsed time from the activation timestamp, not a count of
timer callbacks. A late callback therefore skips to the correct point and
still reaches the target at the requested duration. At the end, write the
exact target V and complete. A zero duration applies the scaled target
immediately.

Keep H and S constant. To avoid repeated lossy 8-bit HSV round-trips, the
proposed implementation captures the starting RGB and scales its channels
proportionally to each interpolated V. This is mathematically equivalent to
changing HSV V with H and S fixed; integer channel rounding is the remaining
quantization. If the starting V is zero, multiplication cannot recover a
chromatic color; callers must set a color before fading upward.

Use a 16-bit unsigned percentage and millisecond duration in the command
payload. This represents 400% and keeps the 2-second duration compact; the
representable duration limit is 65,535 ms. Clamp the computed target to 255.
Use round-to-nearest so the palette's 25% then 400% cycle restores V=255
(`255 -> 64 -> 255` after saturation), V=180 (`180 -> 45 -> 180`), V=80
(`80 -> 20 -> 80`), and V=160 (`160 -> 40 -> 160`).

Use a proposed 20 ms shared update grid. Seven independently phased Fades
otherwise create up to seven distinct 50 Hz deadline streams (about 350
controller wakeups per second in the fully overlapped case). Aligning regular
updates to one monotonic grid lets all active Fades update in one `Run()` and
one strip commit. Keep exact duration endpoints even when they fall between
grid ticks. These rates are calculated from the proposed cadence, not measured
on hardware.

### Rainbow4 Program

Reuse the palette in [rainbow2.py][rainbow-palette]. For LED `i`, schedule its
root color set and child activation at:

```text
mark_i = round(i * 2000 / 7) milliseconds
marks  = 0, 286, 571, 857, 1143, 1429, 1714
```

The program is:

| Group | Instructions |
|---|---|
| 0 | Set each LED's initial RGB color at `mark_i`, then `RunGroup` its unique child at the same mark with forever repeats. Insert each set before its matching `RunGroup`. |
| 1–7 | For the matching LED, Fade by 25% at mark 0 for 2,000 ms, then Fade by 400% at mark 2,000 for 2,000 ms. Repeat forever. |

Start the root once with zero additional repeats. The child loops keep the
program active; the root itself does not need to repeat. Each LED has a nominal
four-second cycle, with its start phase staggered from the previous LED by
approximately 286 ms. Repeats remain completion-relative under the existing
group runtime, so timer lateness can add small period jitter.

The proposed single group-aware Fade protocol command uses tag `0x010D` and
payload `mark:u16, percent:u16, duration_ms:u16, index:u8, group_id:u32`
(11 bytes). Group 0 covers root use; no separate legacy and grouped Fade tags
are needed. The host sends the exact sequence reset + 28 instruction commands +
`Start(0)`. Reset aborts active loops but leaves the currently staged LED
colors unchanged.

## Alternatives and Trade-offs

- **Independent child loops (selected):** preserves a nominal 4-second cycle
  per LED and the requested phase offsets. It needs child forever-repeat
  support and the 28-instruction capacity.
- **One repeating root timeline:** avoids forever child repeats but needs at
  least 21 instruction slots and makes the overall pass wait for the latest
  staggered fade. Earlier LEDs then dwell at full brightness longer; their
  cycles are not independent.
- **Perceptual/gamma-corrected brightness:** could make perceived brightness
  more uniform, but is outside the first version. V-linear scaling is
  deterministic and matches the requested percentage operation.

## Edge Cases and Recovery

- `percent == 0` targets black; values over 100% saturate at V=255.
- A black starting pixel remains black under percentage scaling because its
  hue and saturation are undefined.
- A zero duration is an instant operation and does not itself provide a
  positive scheduling boundary. Repeated groups must still be rejected if
  they contain no positive mark or timed boundary.
- Missed timer ticks use actual elapsed time; they do not stretch the fade.
  Timestamp subtraction must retain the controller's wrap-safe contract.
- Multiple Fades targeting one LED are last-writer-wins according to
  controller execution order. `rainbow4` avoids overlap within each child.
- Reset aborts all loops without restoring their initial colors. The
  `rainbow4` host utility should expose a reset/stop action and must not claim
  that a one-way reset write was acknowledged.
- Per-channel integer rounding may change a low RGB channel by one code across
  a down/up cycle. Tests should characterize this for the seven-color palette;
  the command guarantees V scaling, not photometric or byte-identical output.

## Resource Impact

**Calculated from declarations, not measured:** At 28 instruction slots,
`Controller::kMaxExecuting` also grows from 16 to 28. Its three-`uint32_t`
entries add 144 bytes. The controller's group-validation scratch arrays grow
from about 428 to 728 bytes (approximately 300 bytes more on the 32-bit target,
including alignment). Instruction slots, group runtimes, shared indexes, and
the command mailbox also grow. A target build must measure total RAM/stack
impact before hardware acceptance.

**Calculated workload:** `rainbow4` has at most seven active Fade instructions,
each doing constant-time interpolation and three channel calculations per
update. With a shared 20 ms grid, this is about 350 Fade updates and 50 regular
controller/strip commits per second, plus off-grid exact endpoints. The host
program is thirty protocol frames (490 protocol bytes with the proposed
11-byte Fade payload), excluding serial framing. No target timing or ROM delta
has been measured.

## Steps

### Step 1 — Adding and implementing test-driven Fade behavior

**Goal and scope:** First add focused controller tests, then implement the
single-pixel Fade instruction and its fixed-slot storage. Keep the behavior in
the Prism controller layer.

**Prerequisites:** None.

**Completion criteria:**

- Tests cover current staged-color capture at activation, 25% and 400%
  scaling, V saturation, halfway interpolation, exact completion after a late
  tick, zero-duration behavior, black input, replay reset, and one-pixel scope.
- The implementation uses elapsed timestamps, updates the staged strip color,
  requests a show through the controller, and does not sleep or allocate.
- The full palette's RGB channel rounding is characterized by tests.

**Focused validation:** Use the existing fake-time and mock-strip patterns in
[controller_test.cpp][controller-tests]. Run `uv run unit-tests` and
`uv run format --check` on changed C++ files.

**Resource risk:** The Fade member may enlarge the instruction-slot union;
measure this in the target build.

### Step 2 — Enabling forever child groups and required capacity

**Goal and scope:** Allow non-root `RunGroup` instructions to request the
forever sentinel, increase instruction capacity to 28, and preserve cycle and
positive-boundary validation. Update the existing instruction-group design
note to match the new child-repeat policy.

**Prerequisites:** Step 1.

**Completion criteria:**

- Seven unique child groups can be started at staggered root marks and repeat
  concurrently without the root completing.
- Cyclic group graphs, zero-time repeated groups, duplicate active group
  calls, finite-repeat behavior, and reset/abort behavior remain validated.
- The 28-instruction program fits without changing the current 32-bit group
  mask; the maximum stays at 31 instructions unless that mask is redesigned.

**Focused validation:** Add controller tests for concurrent forever children,
the seven-group timeline, the 28-slot boundary, and reset with stale timer
events; run `uv run unit-tests`.

**Resource risk:** Measure controller static storage and APP-task `Start()`
stack use with the target build. The scratch-array figures above are not a
whole-firmware RAM report.

### Step 3 — Wiring the Fade protocol command

**Goal and scope:** Add the one group-aware Fade tag/payload through the
protocol sink, mailbox, and APP dispatcher. Raise protocol handler capacity to
14 and mailbox capacity to 32.

**Prerequisites:** Steps 1–2.

**Completion criteria:**

- `0x010D` is registered without collisions and accepts exactly the proposed
  11-byte payload.
- Decoding, mailbox delivery, and APP dispatch preserve mark, percentage,
  duration, LED index, and group ID.
- Short/overlong payloads are rejected and registration/capacity failures
  remain explicit.

**Focused validation:** Extend [controller_command_test.cpp][command-tests]
for payload decoding, invalid lengths, registration count, dispatch, and
mailbox capacity; run `uv run unit-tests`.

**Resource risk:** Twelve more mailbox entries are reserved to receive the
30-command Rainbow4 burst as one batch. Verify resulting RAM in the target
build.

### Step 4 — Programming and validating Rainbow4

**Goal and scope:** Add the `rainbow4` host command, register it as a project
entry point, and emit the agreed grouped program. Add a stop/reset helper.

**Prerequisites:** Steps 1–3.

**Completion criteria:**

- Fake-serial tests check all thirty frames, order, checksum, group IDs,
  rounded marks, 2,000 ms durations, 25%/400% values, and `Start(0)`.
- The script checks short writes and reports that the protocol cannot confirm
  controller acceptance. Stop sends reset and does not claim to restore color.
- Existing `rainbow3` behavior and tests remain unchanged.

**Focused validation:** Use standard-library `unittest` tests following
[test_rainbow3.py][rainbow3-tests], run the focused `rainbow4` test module,
with `uv run python -m unittest discover -s scripts/tests -p test_rainbow4.py`.
Then run `uv run unit-tests`, `uv run format --check`, and `uv run build`.

**Hardware risk:** Build and host tests do not prove physical timing or visual
quality. No flashing is included in this step; a live-board demonstration is a
separate, explicitly requested validation.

## Unresolved Decisions

### Blocking

- None. The percentage multiplier and independent forever child-group
  behavior are user-confirmed.

### Non-blocking

- The proposed instruction capacity is the 28-slot minimum. Raising it to 31
  would provide three spare slots but increases fixed RAM further; keep 28
  unless a measured use case warrants extra capacity.
- The 20 ms shared cadence and linear V response are initial design values.
  Physical timing or perceptual tuning may justify a later adjustment.
- Duration is limited to 65,535 ms in the proposed wire payload. Longer fades
  would require a wider payload field.

## Validation Status

No firmware, host tests, target build, or hardware timing has been run for this
design. The current worktree was clean before this document was added.
