# Gradient instruction design

**Status:** Temporal Gradient direction approved; Rainbow5 Step 5 added for review; implementation not authorized.
**Scope:** A timed color transition based on the existing one-pixel Fade instruction.

## User need and confirmed direction

The requested instruction should smoothly transition from one color to another using a color path that looks even to human vision. The user clarified that Gradient is temporal, not a spatial strip ramp.

The first-cut proposal assumes one pixel per instruction, matching Fade. When the instruction reaches its mark, it captures that pixel's currently staged RGB value as the source, then moves to an explicit destination RGB value over a duration. A range operation would need per-pixel source state and is outside this proposal.

The project represents colors as RGB bytes and offers HSV helpers, but the inspected Prism color contract does not define a calibrated LED color profile. This design therefore treats the bytes as sRGB for interpolation. That is an assumption to review, not a property verified for every LED/board combination.

## Recommended behavior

Expose a grouped Gradient instruction with these conceptual fields:

- mark: when the transition starts, using the existing controller instruction scheduling model;
- index: one LED index;
- target: three destination RGB bytes;
- duration_ms: unsigned duration in milliseconds;
- group_id: existing instruction grouping/replay identity.

For example, a host command could request pixel 3 to transition from its staged color to blue over 1500 ms. The wire format and host spelling should follow the existing Fade command conventions.

At the due mark, Gradient captures the pixel's staged color and the controller timestamp. It converts the captured source and target from sRGB to OKLab once, then linearly interpolates OKLab lightness and opponent-color coordinates by elapsed time. It converts each interpolated sample back to linear sRGB, clamps each linear channel to [0, 1], encodes it as sRGB bytes, and stages it through the Prism Strip API. At or beyond the duration, it writes the exact requested target bytes, avoiding endpoint drift from conversion and rounding.

A zero duration writes the exact target immediately and schedules no further run. For positive durations, request updates on a 20 ms cadence anchored to the transition start, while also scheduling the exact final duration boundary. Late controller runs compute from elapsed time rather than replaying missed samples. Use unsigned timestamp subtraction so the existing 32-bit millisecond clock can wrap safely.

A group replay resets transient timing and source state; the replayed instruction captures the then-current staged color when it starts. Resetting/canceling a transition leaves the last staged sample in place. If multiple Gradient instructions target the same pixel, they follow the controller's existing execution order and the last write in a run wins. An invalid LED index performs no writes and completes without an error response, matching Fade's current behavior.

The controller should continue to own scheduling and output coalescing. Gradient only stages colors through the public Strip contract and requests a show; Controller::Run remains responsible for the single Show call after executing due work.

## Perceptual path and alternatives

Recommend rectangular OKLab interpolation. W3C's CSS Color specification recommends Oklab when perceptually even gradient steps are desired; its gradient examples show sRGB interpolation can produce darker or grayish midpoints. OKLCh is an alternative when preserving vivid chroma is more important, but it requires hue-angle wrap and powerless-hue handling and has greater gamut-mapping complexity. These are standards-based expectations, not measurements on Prism's LEDs. See [CSS Color 4](https://www.w3.org/TR/css-color/) and [CSS Images 4](https://www.w3.org/TR/css-images-4/). The original OKLab derivation is documented by [Björn Ottosson](https://bottosson.github.io/posts/oklab/).

The initial gamut policy is per-channel clipping in linear sRGB. This is simple and bounded but may shift hue for saturated source/target pairs whose OKLab path leaves the sRGB gamut. More sophisticated chroma reduction could preserve hue better but adds work and needs a defined mapping policy. Keep the exact target endpoint regardless of the intermediate gamut policy.

## Arithmetic and memory approach

Use fixed-point arithmetic and no runtime floating point or heap allocation. This keeps behavior consistent between the project's Cortex-M0+ SAMD21 and Cortex-M4F nRF52840 targets; the latter has an FPU, but using it would not improve cross-target determinism by itself. Hardware facts are from the [Microchip ATSAMD21 product page](https://www.microchip.com/en-us/product/ATsamd21g18) and [Nordic nRF52840 product specification](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html).

A practical proposal is to store source and destination OKLab coordinates in signed Q2.13 values, with 32-bit intermediates for matrix products, interpolation, and inverse conversion. Use a 256-entry 16-bit sRGB-to-linear lookup table (512 bytes of read-only data). Perform the forward OKLab cube-root conversion only when a transition starts; use a bounded integer cube-root routine rather than a large table. The inverse transform uses fixed-point products and cubes. Convert linear output back to an 8-bit sRGB code with a binary search over the monotonic decode table. Coefficient scaling, rounding, and saturation details belong in implementation tests; range analysis must show intermediate bounds before code is accepted.

**Estimated RAM:** Gradient state needs six 16-bit endpoint coordinates plus start time, target bytes, duration, index, and lifecycle state. Based on the existing Fade slot and expected alignment, budget about 8 bytes extra per instruction slot, or about 224 bytes for all 28 slots. This is an estimate, not a measured target ABI result. The acceptance gate is no more than 256 additional bytes for the instruction pool; verify sizeof and linked static RAM on each target.

**Estimated Flash:** The proposed decode table is exactly 512 bytes. Conversion code size is not knowable from source alone; measure the linked delta for each target. No additional runtime table is proposed.

**Estimated processing:** Each active update is O(1) and performs one inverse transform plus three bounded table searches. At the 20 ms cadence, 28 active instructions imply at most 1,400 updates per second and 28 pixel writes per controller tick, followed by the controller's coalesced show. This is a workload estimate, not a cycle measurement. The SAMD21 is the likely timing constraint.

## Codebase fit and compatibility

- Keep the feature in APP: follow the public instruction API in include/prism/controller_instruction.hpp, implementation pattern in app/controller/instructions/fade.cpp, and storage/tag pattern in include/prism/instruction_types.hpp. Do not add Zephyr or board dependencies.
- Add a Gradient runtime type and payload to the existing typed instruction storage. The current controller has a fixed 28-slot pool; this proposal does not change its capacity.
- Add a new protocol tag after Fade, proposed as 0x010E. A grouped payload can be 12 bytes: mark (u16), target RGB (3 bytes), duration_ms (u16), index (u8), and group_id (u32), in the protocol's existing byte order. Require the exact length and decode fields explicitly.
- The current protocol handler limit is 14 and is fully occupied by 13 APP handlers plus loopback. Registering Gradient therefore requires raising the limit to 15. Existing tags and payloads remain unchanged.
- Update the command payload/dispatcher in app/src/hw/controller_command.hpp, app/src/hw/controller_command_sink.cpp, and app/src/controller_command_dispatch.cpp. Add the instruction source to app/CMakeLists.txt and tests/CMakeLists.txt, as is done for Fade. The 32-entry command mailbox remains unchanged.
- Use the existing controller timestamp and RequestShow() flow. Do not add an OSHAL timer or bypass the Strip API.
- Preserve existing behavior for other instructions. A host command builder may be added under scripts/ after the receiver path is tested; its one-way protocol must not claim command acceptance without an acknowledgement.

Relevant implementation references: [Fade design](fade_instruction.md), [instruction groups](instruction_groups.md), [timed instruction extension](timed_instruction_extension.md), include/prism/controller_instruction.hpp, include/prism/instruction_types.hpp, and app/controller/controller.cpp.

## Validation gates

1. Color math tests compare fixed-point samples against a double-precision OKLab reference using the same clipping and rounding policy. Across a fixed matrix of RGB primaries, secondaries, grays, and representative mixed colors at 0%, 25%, 50%, 75%, and 100% elapsed time, each encoded channel must be within 1 code value of the reference. Both endpoints must match the requested RGB bytes exactly. Include black/white, saturated red/blue, near-gray, and out-of-gamut paths.
2. Instruction tests cover zero duration, exact completion, off-grid duration completion, 32-bit timestamp wrap, invalid pixel index, same-mark source seeding by a preceding SetColor, group replay recapture, cancellation, and two gradients writing the same pixel.
3. Decoder tests accept only the proposed 12-byte payload and reject truncated or extended payloads. Accept every mark and duration value representable by the existing fields, including zero duration; add no new field restrictions. Existing command tags must retain their current bytes and behavior. Confirm the registered handler count is 15 without exceeding the revised limit.
4. Pending hardware timing gate: on SAMD21, measure Controller::Run with 28 active Gradient instructions and require instruction work to finish within 10 ms, half the proposed 20 ms update interval. Also verify the output path still coalesces to at most one Strip::Show per run. This measurement is separate from host checks and target builds and remains pending until hardware validation is authorized. If the budget fails, revisit the inverse encoding search or cadence before adding a larger lookup table.
5. Confirm no heap use, instruction-pool static RAM growth of at most 256 bytes, and a 512-byte maximum for the new color conversion table. Measure linked Flash delta per target and report it; the repository does not establish a feature-specific code-size budget.
6. Run the documented focused host tests and format check, then the XIAO nRF52840 Sense target build. Step 5 adds focused Rainbow5 host tests. Hardware color acceptance is separate: visually inspect representative transitions and measure timing/output behavior only after hardware validation is authorized.

## Steps

### 1. Add and validate fixed-point OKLab conversion

Implement a private APP color helper with the bounded decode table, forward and inverse transforms, clipping, and deterministic rounding. Keep conversion details out of public headers unless an existing pattern requires them.

Completion criteria: reference-vector tests pass the one-code-value gate, exact endpoint conversion is covered, intermediate ranges are proven, and helper table storage is at most 512 bytes.

Validation: focused host unit tests and the project's format check. Record linked Flash and static RAM deltas later when the helper is integrated into the target image. Risk to measure: worst-case integer arithmetic time on SAMD21.

### 2. Add the timed one-pixel instruction

Add Gradient state and execution using the shared timestamp, staged Strip color, 20 ms update grid, exact endpoint, reset/replay semantics, and 28-slot fixed pool. Keep source capture at activation rather than command receipt.

Completion criteria: lifecycle tests pass all cases in Validation gate 2; no dynamic allocation; instruction-pool RAM growth stays within 256 bytes.

Prerequisite: Step 1 conversion helper and its accuracy gate pass.

Validation: focused controller/instruction host tests and format check. Risk to measure: 28 simultaneous conversions fit the 10 ms Controller::Run budget on SAMD21.

### 3. Integrate the grouped protocol command

Add tag 0x010E, the 12-byte payload, dispatcher/sink registration, and handler-capacity update. Preserve old command encodings.

Completion criteria: exact-length decoding, malformed payload rejection, handler count 15, old tag compatibility, and Gradient dispatch into the fixed instruction pool are covered by tests.

Prerequisite: Step 2's instruction contract is stable.

Validation: protocol and app command tests plus format check. Keep the existing 32-command mailbox capacity.

### 4. Add host invocation and verify target footprint

Add a host-side command helper consistent with existing scripts, then run the documented unit-test, format-check, and target-build wrappers. Record both target RAM/Flash deltas and the fixed-point comparison result. Keep SAMD21 runtime measurement in the separate pending hardware timing gate.

Completion criteria: documented host checks and the XIAO nRF52840 Sense target build pass; target RAM/Flash deltas are recorded for each supported target; host documentation states the command is one-way and has no acceptance acknowledgement. Runtime timing and visual color acceptance remain separate hardware gates.

Prerequisite: Step 3 passes protocol and dispatcher tests.

Validation: project-owned host test and format wrappers, followed by the documented XIAO nRF52840 Sense target build. A hardware transition and timing check is a separate, user-authorized validation step.

### 5. Add the Rainbow5 seven-pixel effect

Implement a host-authored repeating effect for six LEDs arranged as a ring and one center LED. It uses the Step 3 grouped Gradient command, the Step 4 host command builder, and the root-repeat behavior already defined by the controller. Keep the effect in one root group; it needs no RunGroup instructions.

The physical wiring order is not assumed. The host invocation takes an ordered list of six ring indices plus a center index. Require six distinct integer indices in 0..6, require the center index to be the remaining value, and validate that the combined mapping covers each index 0..6 exactly once before opening the serial port. For example only, an invocation might use ring indices 0,1,2,3,4,5 and center index 6; users supply their actual layout.

Use T = 1500 ms for each ring segment. For logical ring position i in 0..5, use base hue 60 * i degrees. Its four destination waypoints are HSV hues (base - 90 * j) modulo 360 for j in 0..3, with saturation 100% and value 75%. This decreasing temporal hue phase makes the rainbow bands travel toward increasing logical ring indices when the supplied ring list follows that direction. Describe the effect as a circulating, phase-shifted rainbow; clockwise or counter-clockwise depends on the user-supplied physical mapping. Convert these waypoints to sRGB bytes on the host; the firmware still interpolates each segment in OKLab.

For the center, propose warm white sRGB #FFF0D6 and lavender sRGB #C8B8FF. Move to warm white at mark 0 over 3000 ms, then to lavender at mark 3000 over 3000 ms. Treat these as initial palette constants for visual review, not firmware color-space behavior.

All 26 Gradient instructions belong to group 0:

| Relative mark | Ring action | Center action |
| ---: | --- | --- |
| 0 ms | Waypoint 0 for all six LEDs, duration 1500 ms | Warm white, duration 3000 ms |
| 1500 ms | Waypoint 1 for all six LEDs, duration 1500 ms | Continue toward warm white |
| 3000 ms | Waypoint 2 for all six LEDs, duration 1500 ms | Lavender, duration 3000 ms |
| 4500 ms | Waypoint 3 for all six LEDs, duration 1500 ms | Continue toward lavender |
| 6000 ms | All transitions have reached their endpoints; the root pass may restart | All transitions have reached their endpoints |

Each ring LED uses four Gradient slots, for 24 total; the center uses two, for 26 total. The host sends Reset, 26 grouped Gradient commands, and Start with root_additional_repeats = 0xFFFF, for 28 command frames. This stays within the 28-instruction pool and 32-command mailbox, leaving two instruction slots and four mailbox entries unused. Do not add SetColor initialization commands: the first ring and center gradients capture each LED's currently staged color on the first pass. Later passes start from the previous pass's exact endpoints.

The ring segment marks and durations, plus the two center transitions, give the root a positive timed boundary at nominal 4T = 6000 ms. The root repeats only after every active Gradient has completed; it starts the next pass at that completion timestamp, making mark-zero instructions due immediately on the next controller progress pass. This is completion-relative, so scheduler lateness can add small cycle jitter; six seconds is nominal, not a wall-clock guarantee. The seam returns each ring LED from waypoint 3 to waypoint 0 and the center from lavender to warm white through another OKLab transition. A reset/stop aborts the root loop but leaves the last staged colors in place.

Add a scripts/rainbow5.py entrypoint, register uv run rainbow5 in pyproject.toml, add scripts/tests/test_rainbow5.py, and document invocation and mapping in README.md. Follow the existing Rainbow4 host-programming pattern and protocol helpers. Include a --stop path that sends Reset only; report that the one-way protocol has no acceptance acknowledgement and that stop leaves the displayed colors unchanged.

Completion criteria: the generated program contains exactly 26 grouped Gradient instructions and no initialization or RunGroup frames; mapping validation is performed before opening the serial device; the root starts with the forever sentinel; the host writes 28 frames at most; and the six-second timeline and stop behavior are covered by tests.

Focused validation:

- Fake-serial tests decode every frame and checksum, assert Reset + 26 Gradient + Start ordering, all grouped payload fields, four ring waypoints at marks 0, 1500, 3000, and 4500 ms with 1500 ms duration, two center transitions at marks 0 and 3000 ms with 3000 ms duration, and Start(0xFFFF). Assert there are no SetColor or RunGroup frames.
- Mapping tests accept arbitrary valid permutations of physical indices and reject duplicates, missing indices, out-of-range values, and a center index that overlaps the ring; invalid mappings must fail before opening serial.
- A controller timeline test advances fake timestamps through at least two root passes, checks that all transitions reach endpoints at the 4T boundary, that the next mark-zero pass begins without an intentional idle interval, and that Reset prevents later Gradient writes. Keep the host planner test and controller repeat test separate so one does not duplicate the other's implementation.
- Run uv run python -m unittest discover -s scripts/tests -p test_rainbow5.py, the documented Python lint wrapper, project host tests, format check, and the XIAO nRF52840 Sense target build. No live serial device or target is used for these checks.

Resource impact: no new firmware instruction type or capacity is required beyond Gradient. The animation occupies 26 of 28 instruction slots and emits 28 command frames into a 32-entry mailbox. Host palette generation is bounded to 24 HSV-to-RGB waypoint conversions per program build. Visual confirmation of the actual wiring map, hue balance, center palette, and repeat seam is a separate hardware acceptance gate requiring explicit authorization.

## Review and implementation boundaries

The user approved the temporal Gradient direction and requested the Rainbow5 effect in Step 5. This revision records that design direction; it does not authorize source, test, script, configuration, build, or hardware changes. Separate explicit implementation authorization is required. Once authorized, Mid implements the numbered steps in order and returns focused diff, test, build, and resource evidence. The user reviews each completed step before dependent work proceeds. Auron performs an independent implementation review after the user reviews the step evidence. Any review that changes the approved design returns for user review.

## Approved direction and remaining deployment inputs

The approved baseline is a one-pixel timed instruction, sRGB byte interpretation, rectangular OKLab interpolation using fixed-point arithmetic, and simple linear-channel clipping, with exact requested endpoints. Rainbow5 uses one root group and the capacity counts in Step 5.

The Rainbow5 ring mapping is provided at host invocation; the example index order is illustrative and does not assert physical wiring. The HSV brightness and center RGB constants in Step 5 are proposed effect settings. Visual tuning against the actual strip remains a separate hardware acceptance gate. The pending SAMD21 timing measurement and the visual gate are not part of host validation or the target build.


## Sources

- Existing project contracts: app/controller/docs/fade_instruction.md, app/controller/docs/instruction_groups.md, app/controller/docs/timed_instruction_extension.md, include/prism/controller_instruction.hpp, include/prism/instruction_types.hpp, app/controller/controller.cpp, and the APP command/protocol files listed above.
- W3C, [CSS Color Module Level 4](https://www.w3.org/TR/css-color/) and [CSS Images Module Level 4](https://www.w3.org/TR/css-images-4/) for color-space and interpolation behavior.
- Björn Ottosson, [A perceptual color space for image processing](https://bottosson.github.io/posts/oklab/), for the OKLab derivation.
- [Microchip ATSAMD21](https://www.microchip.com/en-us/product/ATsamd21g18) and [Nordic nRF52840 product specification](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html) for target CPU architecture references.
