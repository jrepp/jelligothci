---
id: adr-003
title: Start with a shapes-only MVP
status: Accepted
created: 2026-10-07
deciders: Jacob Repp
tags: [mvp, rendering, scope]
project_id: jelligotchi
doc_uuid: 6d0a0016-3f99-4b5f-9d1c-f82ad370030e
---

# Context

The long-term goal is a Tamagotchi-like pet. The immediate task is to establish
a local drawing test and a portable engine boundary. An early implementation
started adding creature state and a pet UI before the scope was narrowed.

# Decision

Limit the first MVP to drawing a few simple shapes. Use it to exercise the
surface, timing, input, and presentation contracts before building pet behavior.

The current scene contains a square, a triangle, and an animated circle. A tap
or Space toggles pause. Its 466 by 466 surface and circular mask match the
initial board target. Creature needs, lifecycle, persistence, richer UI, audio,
and power management remain outside this increment.

# Approval basis

Jacob instructed, "focus on just drawing a few simple shapes as an intial MVP",
in the 2026-10-07 session, then requested that the shapes demo be started.

# Consequences

- Rendering and platform integration can be checked with a small program.
- The core interfaces can evolve before creature behavior depends on them.
- The demo does not yet prove a complete pet system or its resource needs.
- A passing desktop test and firmware build do not prove device behavior.
  Physical display and touch checks remain part of later bring-up.

# Alternatives

Continuing directly with pet stats, saving, and UI would expand the first
milestone beyond the user's requested scope. A static image alone would not
exercise the timing and input contracts already required by the architecture.

# References

- [Shared core](adr-001-portable-c-core-and-host-adapters.md)
- [Injected timing](adr-002-inject-time-and-keep-pacing-in-hosts.md)
- [Shape renderer](../../core/render.c)
- [Initial validation memo](../memos/memo-001-shapes-mvp-foundation.md)
