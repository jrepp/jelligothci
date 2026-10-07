---
id: adr-007
title: Use bounded C code with enforced quality checks
status: Accepted
created: 2026-10-07
deciders: Jacob Repp
tags: [c, embedded, memory, quality, tooling]
project_id: jelligotchi
doc_uuid: 96cb0abc-a3fe-4cc2-8ac1-001501aeccbf
---

# Context

The core will run on a small embedded system. Memory growth, hidden allocation,
undefined behavior, and large functions can make changes difficult to reason
about and expensive to debug on the board.

# Decision

Use resource-conscious, portable C11 with explicit ownership and bounded work.
Prefer fixed storage and caller-owned buffers. Keep the core and steady-state
engine paths free of heap allocation, VLAs, and unbounded recursion. Account
for stack, persistent memory, queue capacity, and transfer sizes when a change
affects those resources. Existing SDK and port startup allocations are separate
from that core guarantee and must remain bounded and checked.

Keep local compilation under CMake with strict warnings treated as errors.
Run C formatting, static analysis, source-size checks, and Docuchango validation
through pinned pre-commit tools and CI. Store pins and thresholds in Git and
download tooling into ignored local directories.

The initial gates use clang-format for project C, clang-tidy for core/tests,
Cppcheck for all project C, and Lizard for function metrics. Limits are 80
function NLOC, 400 physical lines per C/header file, and complexity 20. These
are initial enforcement settings, not claims about ideal sizes for all code.
Refactor responsibilities before considering a justified threshold change.

Keep reusable instructions in the versioned embedded-development and safe-C
skills, with short task routing in `AGENTS.md`. Static analysis supplements
tests and hardware measurements; it does not establish memory or timing safety
by itself.

# Approval basis

In the 2026-10-07 session, Jacob requested embedded-development and modern safe-C
skills that avoid dynamic allocation and respect the small system. He also
requested pre-commit C lint and Docuchango validation, static analysis, limits
on function and file growth, and CMake compilation with warnings as errors.

# Consequences

- Staged changes are checked locally before commit; CI repeats the checks.
- Clear budgets and contracts reduce hidden resource costs.
- First use downloads pinned tools. Subsequent use can reuse local caches.
- Static analysis without vendor headers cannot verify every adapter API.
  Firmware builds and hardware testing remain necessary.
- Narrow suppressions must explain a specific modeling gap. The checks must
  not be weakened globally to make an unrelated change pass.
- Introducing the size gate split the SDL entry point into focused helpers.

# Alternatives

Review-only conventions are easy to miss. Compiler warnings alone do not cover
all static-analysis or size concerns. A blanket ban on every SDK startup
allocation would describe a different implementation from the existing port.

# References

- [Pre-commit configuration](../../.pre-commit-config.yaml)
- [C checks](../../scripts/c-lint)
- [Size limits](../../.c-size-limits.json)
- [CMake warning settings](../../CMakeLists.txt)
- [Embedded development skill](../../.agents/skills/jelli-embedded-development/SKILL.md)
- [Safe C skill](../../.agents/skills/jelli-safe-c/SKILL.md)
