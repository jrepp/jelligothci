---
id: memo-036
title: Windows asset path validation failure
author: Codex
created: 2026-10-10
tags: [assets, ci, portability]
project_id: jelligotchi
doc_uuid: df767c04-4133-4fb0-9cc3-0733e386887c
---

# Observation and remedy

Release validation for PRs #10–#12 failed while generating embedded artwork on
Windows. Run 38035704626 reported every PNG twice in the manifest mismatch:
manifest paths used forward slashes, while native Path string conversion used
backslashes. Core-only Windows checks did not exercise the asset generator.

The generator now uses `relative_to(SOURCE).as_posix()` when comparing discovered
PNGs with manifest paths. Paths serialized into the asset format must use its
portable forward-slash convention; native paths remain appropriate for file I/O.
The existing real-art validation and Windows release packaging exercise this
boundary. Do not remove the manifest completeness check or relax compiler flags.

# Verification boundary

Local desktop, core-only, sanitizer, and documentation checks are run during
landing. Native Windows packaging must pass on the repaired PR revision before
merge; a macOS run alone cannot verify Windows path behavior. This change does
not require flashing and does not establish physical display or touch behavior.

# References

- [Failed Windows run](https://github.com/jrepp/jelligothci/actions/runs/38035704626)
- [Asset generator](../../tools/assets/build_slice.py)
- [Process lessons](memo-004-process-learnings-and-context-remediation.md)