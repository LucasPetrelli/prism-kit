# Prism Kit Gradient Step 4 Review Packet

- Project root: `C:\dev\west-zephyr\prism-kit`
- Baseline commit: `6e36566976fdfd43884b4178f9964bffd86132c9`
- Implementation scope: Step 4 host frame builder and one-command invocation only.
- Exact implementation diff: [`step4.diff`](step4.diff)
- Check manifest and raw logs: [`manifest.json`](manifest.json)

## Step 4 changes

- `scripts/modules/protocol.py` adds tag `0x010E` and a grouped Gradient frame builder. Its 12-byte payload is mark:u16 LE, target RGB bytes, duration_ms:u16 LE, index:u8, and group_id:u32 LE.
- `scripts/gradient.py` adds the `uv run gradient` command. It writes Reset, one root-group Gradient, and Start(0), using the command port, 115200 baud by default, existing serial timeout settings, and short-write/error handling.
- `scripts/tests/test_gradient.py` checks exact frame bytes, ordering, XOR checksums, short writes, and CLI routing using fake serial.
- `pyproject.toml` registers the `gradient` entry point.
- `README.md` documents invocation, payload layout, and the one-way protocol. Full serial writes do not confirm controller acceptance.

Step 5 and Rainbow5 are not implemented. The pre-existing untracked design file `app/controller/docs/gradient_instruction.md` was preserved and is not in `step4.diff`.

## Host and project validation

All commands below ran from `C:\dev\west-zephyr\prism-kit`. Each listed log contains raw combined stdout and stderr; `manifest.json` records command, working directory, and exit code.

| Command | Exit code | Raw log |
| --- | ---: | --- |
| `uv run python -m unittest discover -s scripts/tests -p test_gradient.py` | 0 | `focused-python-tests.log` |
| `uv run lint-python` | 0 | `python-lint.log` |
| `uv run lint` | 0 | `project-lint.log` |
| `uv run unit-tests` | 0 | `project-host-tests.log` |
| `uv run format --check` | 0 | `format-check.log` |
| `uv run build --board xiao_nrf52_sense --clean` | 0 | `xiao-nrf52-sense-build.log` |
| `git diff --check` | 0 | `diff-check.log` |

The project host-test wrapper passed both suites: 118 controller tests and 34 protocol tests. These C++ suites are separate from the focused Python tests for the changed host code. `git diff --check` examined tracked files only; Auron also reviewed the new host source and tests. An initial sandboxed Python test attempt could not start because `uv` was denied access to its interpreter cache; partial logs remain in `sandboxed-attempt/`. The requested checks were rerun successfully with approved access.

## Authorized footprint delta

After the initial review, the user authorized the two-target ROM/RAM delta. The report and full build provenance are in [`footprint-delta-report.md`](../gradient-step-4-footprint-delta-2026-10-18/footprint-delta-report.md). The SAMD21 baseline and candidate both measured 182636 B `FLASH` and 31304 B `RAM`; the XIAO nRF52840 Sense baseline and candidate both measured 185256 B `FLASH` and 29640 B `RAM`. Each measured delta is 0 B. Target-specific configurations matched by `.config` hash. The temporary source snapshots were removed after preserving logs and map-region evidence.

## Validation boundaries

- No flashing, hardware smoke test, or timing validation was performed.
- The linked RAM report is region occupancy; it does not measure runtime stack high-water or dynamic allocation behavior. The full temporary map and `.config` files were deleted after their hashes and exact region excerpts were captured.
- The user explicitly approved staging and committing the reviewed Step 4 result after Auron review.

## Independent review

Auron reviewed the exact Step 4 diff, candidate source hashes, host/build evidence, both footprint comparisons, configuration/toolchain provenance, and calculations. Auron reported no actionable findings. The user approved staging and commit after this final review.
