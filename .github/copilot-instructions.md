# Repository Instructions

## Language Preference

- Prefer modern C++ for new code whenever the Zephyr target, toolchain, and
  repo configuration support it cleanly.
- Default to C++ for new application, board-abstraction, and OS-abstraction
  logic unless there is a specific reason to stay in C.
- Use C only where the boundary is naturally C-shaped, such as Zephyr startup
  hooks, ISR entry points, low-level callback glue, or places where C macros and
  ABI expectations make C the simpler choice.
- When a C boundary is required, keep that boundary thin and forward into C++
  implementation code instead of spreading C across the module.

## Embedded C++ Style

- Prefer `.hpp` for public headers and `.cpp` for implementations when adding
  new C++ code.
- Prefer clear ownership, RAII, `constexpr`, `enum class`, `std::array`, and
  other small zero-cost abstractions over manual C-style bookkeeping when they
  are appropriate for embedded code.
- Avoid exceptions and RTTI unless a change explicitly enables and justifies
  them for this target.
- Avoid heap allocation in low-level or steady-state paths unless it is clearly
  justified.
- Keep generated code, register-facing code, and timing-sensitive code easy to
  inspect. Do not hide hardware behavior behind clever abstractions.

## Build And Integration

- If a change introduces new C++ source files or standard library usage, update
  the Zephyr configuration and build setup explicitly instead of assuming C++ is
  already enabled.
- Prefer incremental migration: keep existing stable C boundaries in place when
  needed, but place new logic behind a C++ interface where practical.
- For repo-local tooling and automation, prefer Python scripts over PowerShell
  or shell wrappers unless the task genuinely requires a platform-native shell.

## Layering Expectations

- Keep application logic unaware of Zephyr APIs, devicetree details, and raw
  board pin mappings.
- Keep OSHAL, BAL, and APP interfaces narrow, explicit, and documented.
- Prefer C++ in `app/`, `bal/`, and `oshal/` first.
- Keep direct Zephyr init hooks, ISR entry points, and similarly C-shaped
  integration points thin even when they live inside `oshal/`.

## API Documentation Format

- Use Doxygen-style comments for public types, functions, methods, and any
  non-trivial constants or data members that define the API contract.
- Prefer `///` comments on declarations in headers so the documentation stays
  close to the public interface.
- For public functions and methods, document at least:
  - `@brief`
  - `@param` for every parameter
  - `@return` for non-`void` results
  - `@tparam` for templates
- Use `@note`, `@warning`, `@pre`, and `@post` when ISR safety, threading,
  timing, ownership, initialization order, or board assumptions matter.
- Keep implementation comments focused on intent, invariants, and hardware
  constraints instead of narrating obvious code.

## Source File Comments

- Add concise comments to source files when a section, ordered code path, or
  hardware-facing sequence would otherwise require mental unpacking.
- Prefer comments that explain intent, ordering, and why a choice exists over
  comments that restate symbol names or literal operations.
- When documenting execution paths, keep the wording plain and short; avoid
  stacking jargon where a simple explanation is enough.

## Documentation Example

```cpp
/// @brief Set the logical state of a board-owned LED.
/// @param led LED instance previously initialized by the board layer.
/// @param on True turns the LED on, false turns it off.
/// @return STATUS_OK on success, or a negative project-defined status code on failure.
int bal_led_set(const Led& led, bool on);
```

## Naming And Contracts

- Prefer descriptive type and function names over project-wide prefixes unless a
  collision risk is real.
- At C boundaries, explicit status returns are preferred over hidden global
  state.
- Public headers should document what a caller may assume, what they must
  provide, and what failure modes to expect.

## Commit Message Format

- When generating commit messages, use the header format `tag: action`.
- Keep the header imperative, specific, and short enough to scan quickly.
- Prefer modern change tags such as `feat`, `fix`, `refactor`, `docs`, `build`,
  `test`, `ci`, and `chore`.
- Choose the tag based on the user-visible or maintenance intent of the change,
  not simply the largest file touched.
- The optional body should explain how the action was achieved by summarizing the
  touched files or change areas.
- Keep the body concise and factual. Prefer one short paragraph or a few flat
  `-` bullets when multiple touched areas need to be called out.
- Do not narrate git operations, generated files, or unrelated workspace noise.

Example header:

```text
refactor: merge HAL and OSAL into OSHAL
```

Example with optional body:

```text
refactor: merge HAL and OSAL into OSHAL

- collapse Zephyr boundary services under `oshal/`
- keep the `SYS_INIT()` hook thin while preserving the C ABI
- update README and repo instructions to document the new layering
```