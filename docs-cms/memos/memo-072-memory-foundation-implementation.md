---
title: Checked memory and scratch-arena foundation implementation
author: Codex
created: 2026-10-19
tags: [implementation, memory, portability, testing]
id: memo-072
project_id: jelligotchi
doc_uuid: fab3f8c3-c216-4895-b941-66695ebe84f5
---

# Implemented increment

Following the owner's direction to continue into implementation, this increment
implements the checked-storage foundation in
[RFC-009](../rfcs/rfc-009-bounded-memory-systems-and-adoption.md).
The initial measurements below used an uncommitted change based on
`d830df7374ca3e72ed0608f6341b0776d0ed29e0`; validation below applies to this
working-tree increment, not the base revision alone.
The implementation/test set has SHA256
`4bc4058747ac7abb4f3aab72d501567d2905f1c41664122f547d3942791a7fc6`.
This hashes sorted repository-relative names, a zero byte, then each file's
contents for `CMakeLists.txt`, `core/memory.c`, `include/jelli/memory.h`,
`tests/test_memory.c`, and `tests/test_memory_arena.c`.

The [public API](../../include/jelli/memory.h) and
[portable implementation](../../core/memory.c) provide:

- Checked size addition, multiplication, and supported alignment.
- Bounded byte subviews, including explicit empty/null behavior.
- Aligned allocations from caller-owned scratch storage, with high-water usage.
- Arena marks bound to identity and epoch; rewind/reset reject stale marks.
- Exclusive checkout/checkin tokens that freeze arena mutations until returned.
- Explicit failure statuses and unchanged outputs/state on failure.

No API allocates or frees backing memory. The arena object and backing storage
must be disjoint; supported alignment and initialization preconditions are
defined in the header. Rewind/reset invalidate all marks, including on a no-op
rewind, to prevent offset reuse from resurrecting a stale identity. Epoch and
lease-generation exhaustion fail instead of wrapping.

Checkout is an ownership guard for previously allocated views, not a mutex,
thread executor, or proof that another thread has stopped using a pointer.
Callers supply synchronization and check in only after the worker is done.
Reset still requires all reclaimed views to be unused. Public fields allow
caller-owned structs and inspection; callers must not modify them.

[CMake](../../CMakeLists.txt) links the foundation into the native shared core
and registers independent tests in core-only and rendered configurations.
The renderer, desktop reload banks, and firmware buffers retain their existing
storage paths. Pools, the Jellipak C reader, native worker execution, and firmware
adoption remain subsequent increments.

# Edge-case verification

[Arithmetic/span tests](../../tests/test_memory.c) cover overflow, exact bounds,
null/empty input, one-past empty views, supported/invalid alignment, and unchanged
outputs. [Arena tests](../../tests/test_memory_arena.c) cover aligned placement,
padding-only exhaustion, exact fit, guards around backing storage, high-water
tracking, foreign/stale marks, offset reuse, checkout exclusion, stale and
duplicate lease return, and exhaustion of identity counters.

Counter-limit tests deliberately inject exhausted public fields as a test-only
shortcut; normal callers may not mutate those fields. Tests use explicit checks
that remain active with `NDEBUG`.

Independent implementation, test-authoring, and review passes found two test
portability issues before acceptance: an exact-fill fixture could request zero
bytes on one alignment, and whole-struct comparison included padding. The fixture
now derives capacity from alignment, and the state comparison names fields.
No runtime warning policy was weakened.

An initial static-analysis run also overlapped a header comment edit and reported
truncated declarations. The source was held stable and the complete C lint suite
rerun successfully. Future parallel validation should share stable code inputs;
documentation can be updated independently.

# Validation and remaining evidence

Native macOS validation:

| Check | Result |
| --- | --- |
| `make core-test` | All 35 tests passed, including both memory suites |
| `make test` | All 76 tests passed, including desktop and asset integration |
| `make lint-c` | Formatting, clang-tidy, Cppcheck, and source-size gates passed |
| `make sanitize` | All 76 tests passed with the ASan/UBSan build |

The independent test-authoring pass also ran the new tests under a strict C11
compiler with ASan/UBSan and ran arena tests with `NDEBUG`. The full project
sanitizer result is recorded separately above. `make docs-check`,
`make docs-fix`, and `git diff --check` also passed after the evidence record
was finalized, with no remaining documentation findings.

No firmware compilation, device heap trace, physical display/touch test, or
multithreaded runtime qualification is established by this increment. Its
checkout API remains externally synchronized. The source audit and resource
tradeoffs remain recorded in
[memo-071](memo-071-allocation-audit-and-memory-trajectories.md).

# Publication portability follow-up

[PR #71](https://github.com/jrepp/jelligothci/pull/71) publishes this foundation
with the subsequent Jellipak and authoring increments. Its first Windows CI
build failed because MSVC's C library does not define `max_align_t`.
The public header now provides `JelliMemoryAlignment`: the standard type on
other supported toolchains and a union covering fundamental scalar alignment
on MSVC. Arena storage and alignment validation use that same public type.
This preserves the existing alignment contract on macOS and Linux without
defining a reserved standard-library name.

Native arithmetic/arena tests and a strict C11 build exercising the fallback
branch passed. The fallback branch check on a host compiler does not substitute
for the Windows CI build; that build is the platform acceptance check.
