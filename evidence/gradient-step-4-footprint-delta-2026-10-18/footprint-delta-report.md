## Embedded target ROM/RAM delta — Prism Kit Gradient Step 4

- **Status:** DELTA AVAILABLE
- **Project / Step:** `C:\dev\west-zephyr\prism-kit` / Gradient Step 4
- **Comparison:** baseline commit `6e36566976fdfd43884b4178f9964bffd86132c9` → that commit plus the reviewed, uncommitted Step 4 source overlay. The exact candidate file hashes are in `candidate-source-sha256.txt`.
- **Target / image:** Standard Prism Kit firmware image for the Seeeduino XIAO SAMD21 and XIAO nRF52840 Sense.
- **Comparability:** MATCH for each board. Baseline and candidate used the same board target, `.config` hash, pinned `bal`/`oshal` commits, Zephyr 4.3.0 workspace, GNU Arm Embedded GCC 14.2.1 and GNU ld 2.43.1, and standard optimized image settings. The Step 4 change is host tooling and documentation.

### Build provenance

| Side | Source ID / state | Target and config | Toolchain / linker | Exact wrapper command and working directory | Exit |
|---|---|---|---|---|---:|
| Baseline | `6e36566976fdfd43884b4178f9964bffd86132c9`, clean source snapshot | SAMD21 `seeeduino_xiao/samd21g18a`; `.config` SHA-256 `0C2A40D8...4418029B` | GCC 14.2.1 / GNU ld 2.43.1 | `uv run build --board seeeduino_xiao --clean --skip-lint` (`tmp/embedded-rom-ram-delta/step4-20261018-run01/baseline`) | 0 |
| Candidate | Step 4 overlay based on `6e36566976fdfd43884b4178f9964bffd86132c9` | SAMD21 `seeeduino_xiao/samd21g18a`; same `.config` SHA-256 | GCC 14.2.1 / GNU ld 2.43.1 | `uv run build --board seeeduino_xiao --clean --skip-lint` (`tmp/embedded-rom-ram-delta/step4-20261018-run01/candidate`) | 0 |
| Baseline | `6e36566976fdfd43884b4178f9964bffd86132c9`, clean source snapshot | Sense `xiao_ble/nrf52840/sense`; `.config` SHA-256 `9AC00AB1...1D18BF70` | GCC 14.2.1 / GNU ld 2.43.1 | `uv run build --board xiao_nrf52_sense --clean --skip-lint` (`tmp/embedded-rom-ram-delta/step4-20261018-run01/baseline`) | 0 |
| Candidate | Step 4 overlay based on `6e36566976fdfd43884b4178f9964bffd86132c9` | Sense `xiao_ble/nrf52840/sense`; same `.config` SHA-256 | GCC 14.2.1 / GNU ld 2.43.1 | `uv run build --board xiao_nrf52_sense --clean --skip-lint` (`tmp/embedded-rom-ram-delta/step4-20261018-run01/candidate`) | 0 |

The full working directories, command results, and raw logs are recorded in `builds.jsonl`. One initial sandboxed SAMD baseline attempt failed before compilation because `uv` could not access its dependency cache/network; its log is preserved. The same documented wrapper then succeeded in the authorized elevated run shown above. Both build sides used identical commands and options for each board.

### Linked resource use

| Target / resource / linker region | Baseline used (B) | Candidate used (B) | Delta (B) | Delta vs baseline (%) | Capacity (B) | Baseline use (%) | Candidate use (%) |
|---|---:|---:|---:|---:|---:|---:|---:|
| SAMD21 Flash — `FLASH` | 182636 | 182636 | 0 | 0.00% | 237568 | 76.88% | 76.88% |
| SAMD21 RAM — `RAM` | 31304 | 31304 | 0 | 0.00% | 32768 | 95.53% | 95.53% |
| XIAO Sense Flash — `FLASH` | 185256 | 185256 | 0 | 0.00% | 806912 | 22.96% | 22.96% |
| XIAO Sense RAM — `RAM` | 29640 | 29640 | 0 | 0.00% | 262144 | 11.31% | 11.31% |

### Measurement evidence and limitations

- **Metric source:** GNU ld `Memory region / Used Size / Region Size / %age Used` summaries in the four successful raw build logs. Exact `FLASH` and `RAM` capacities were checked against the corresponding `Memory Configuration` entries in the map excerpts. `linked-metrics-source.json` records each build log, map hash, config hash, and measured byte count. `IDT_LIST` was reported as 0 B used and is not part of the target `FLASH` or `RAM` totals.
- **Temporary checkout cleanup:** removed the verified run-specific checkout `tmp/embedded-rom-ram-delta/step4-20261018-run01` after preserving the linker summaries, map-region excerpts, hashes, and build logs.
- **Scope / exclusions:** Flash is the linked `FLASH` region; RAM is the linker-reported `RAM` region. RAM occupancy is not a runtime stack high-water or dynamic-allocation measurement. The report does not include hardware behavior.
- **Limitations:** The linker emitted the same RWX LOAD segment warning on each build. Full generated map and `.config` files were in the deleted temporary checkout; their SHA-256 hashes and map-region excerpts remain, but the hashes cannot now be recomputed. The raw build logs retain the primary linked-region summaries. No flashing, hardware smoke test, or timing validation was performed.
- **Delta availability:** Available. Both source snapshots built successfully for both targets with matching configurations and metric sources; each measured delta is 0 B.
