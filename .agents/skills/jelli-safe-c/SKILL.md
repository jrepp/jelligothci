---
name: jelli-safe-c
description: Write, refactor, or review Jelligotchi C code for bounded memory and execution, explicit ownership, portable C11, and strict compiler/static-analysis checks. Use for C changes, including engine APIs and adapters; not for prose-only edits.
---

# Resource-conscious, safe C

Use portable C11 as the baseline in this project. Modern safe C means explicit
contracts and defined behavior, not an automatic switch to C23 or optional
Annex K functions. Keep the same core buildable with GCC, AppleClang, MSVC, and
the ESP32 toolchain. Read the [engine contract](../../../include/jelli/engine.h)
and relevant [ADRs](../../../docs-cms/README.md) before changing an interface.

## Memory and ownership

- Keep the core allocation-free and do not allocate in steady-state frame,
  input, or ISR paths. Prefer caller-owned storage with explicit capacity.
  Do not replace a heap allocation with a large stack object. Avoid VLAs,
  `alloca`, unbounded recursion, and hidden library allocations in hot paths.
- A pointer does not carry capacity. Pass sizes/strides where needed, define
  whether lengths count bytes or elements, and document validity/lifetime.
  Use `const` for read-only access without casting away qualifiers.
- Check externally supplied dimensions, offsets, and counts before indexing or
  arithmetic. For unsigned bounds, compare `length <= capacity - offset` only
  after establishing `offset <= capacity`. Check multiplication before forming
  allocation sizes or pitches; widening after an overflow is too late.
- Define ownership on failure as well as success. Keep outputs unchanged on
  failed initialization where that is the contract; otherwise document the
  partial state and cleanup path. Null and zero-length cases must be deliberate.
- Keep large persistent or scratch buffers out of small task stacks. Bounded
  startup allocations may remain in platform adapters when justified by SDK or
  PSRAM needs; they do not permit heap use inside the portable core.

## Defined behavior and bounded execution

- Use `size_t` for sizes and fixed-width integers for hardware or serialized
  fields. Convert only after range checks when a value can exceed the target
  type. Do not silence conversion warnings with unchecked casts.
- Avoid signed overflow, invalid shift counts, and mixed-sign comparisons.
  Where unsigned wrap is intentional, document the range and rollover model.
  Do not assume desktop `long`, pointer size, alignment, or byte order on ESP32.
- Do not dereference unaligned byte buffers as typed structs, violate aliasing,
  or serialize a native struct's padding/layout. Decode fields explicitly;
  use `memcpy` for valid non-overlapping object copies and `memmove` for overlap.
- Give loops, queues, retry logic, and event batches explicit bounds. Return a
  meaningful error for capacity exhaustion. Keep latency-sensitive callbacks
  free of blocking work. Apply the injected clock rather than adding sleeps.
- Avoid unbounded string copies and nonliteral printf format strings. With
  `snprintf`, check negative results and truncation before using the result as
  an offset or length. An `_s` suffix alone does not establish safety.
- Use assertions for programmer invariants, not as the only check on input or
  runtime failures. Preserve release-build checks where correctness needs them.
- Prefer small functions with one clear job and private `static` helpers.
  Use enums, designated initializers, and compile-time assertions when they
  clarify a real invariant. Avoid macros with repeated argument evaluation.

## Enforced project checks

Build through CMake (`make test` / `make core-test`). The project enables strict
GCC/Clang warning sets and MSVC `/W4`, with warnings as errors. Keep diagnostics
visible; do not disable warning groups globally to get a build through.

Run `make lint-c` for clang-format, clang-tidy, Cppcheck, and source-size checks.
Run `make format-c` for formatting repairs. Clang-tidy checks the portable core
and tests; Cppcheck also checks both adapters without requiring their SDKs.
That header-independent analysis does not validate every vendor API contract.
Use firmware compilation and targeted tests for those boundaries.

The authoritative limits live in [.c-size-limits.json](../../../.c-size-limits.json):
80 nonblank, noncomment lines per function, 400 physical lines per C/header
file, and cyclomatic complexity 20. Split by responsibility when a limit is
exceeded. Do not compress statements, hide work in macros, or raise a limit
just to pass. Explain a necessary exception rather than silently weakening
checks. A diagnostic suppression must be narrow and explain the specific
false positive or supported API contract.

For shared logic changes, run `make test`, `make core-test`, and `make sanitize`.
Test relevant edge cases: empty/full capacity, boundary coordinates, counter
limits, time rollover, allocation failure in a host, and callback ownership.
Avoid redundant tests that merely repeat the implementation. After formatting,
review the diff for unintended changes; after refactoring, verify behavior.
