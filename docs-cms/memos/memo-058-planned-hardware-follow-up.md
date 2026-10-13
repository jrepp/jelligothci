---
title: Planned hardware feature follow-up
author: Codex
created: 2026-10-11
tags: [embedded, planning, validation]
id: memo-058
project_id: jelligotchi
doc_uuid: 60529728-786a-4cc3-a234-ea538c6b9ca8
---

# Scope

Followed up on unimplemented planned hardware features against source at
`ab2b6f7`, the ranked validation plan, memo-047, memo-052, memo-056 and Draft
RFC-003. This is a documentation reconciliation, with no firmware or gameplay
policy changes. It does not approve proposal defaults.

# Findings

The ranked plan still described all post-inventory cases as unimplemented
factory commands. Several now have power-profile diagnostics. Updated the
[validation plan](../../docs/device-validation-passes.md#implementation-status-and-next-work)
with source-linked status, missing implementation and remaining validation gates.

Automatic device sleep, qualified touch/IMU wake, product brightness policy,
specialized rendering, TE scheduling, peripheral/PMIC power policy and advanced
touch/display modes remain outstanding. Existing touch recovery, panel trials,
RTC progression, IMU qualification, profiling and staging experiments should be
validated and extended rather than recreated. AP/browser setup and authenticated
network debug remain proposed alongside implemented USB-configured networking.

The immediate bench dependency is the unresolved recovery in
[memo-056](memo-056-hardware-optimization-continuation.md). The first-refresh
startup fix is present in source; that is not proof it has run successfully on
the board. After recovery, collect a complete pipeline baseline and run the
existing staging comparator before selecting an optimization. Separate render
and LVGL attribution is a bounded software-only follow-up candidate.

# Validation and uncertainty

This review inspected source/profile inclusion and historical evidence. It did
not discover or access a device, build firmware, run host tests, flash an image,
or measure physical display, touch, wake or current. Current device availability
and firmware state remain unknown. `make docs-check` and `make docs-fix` passed
for 79 documents without repairs; `git diff --check` passed. Earlier bench
measurements remain dated evidence in their owning memos.
