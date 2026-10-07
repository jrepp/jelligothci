---
id: adr-002
title: Inject time and keep frame pacing in the hosts
status: Accepted
created: 2026-10-07
deciders: Jacob Repp
tags: [architecture, testing, timing]
project_id: jelligotchi
doc_uuid: aa2393e9-db0d-46a4-8a6b-b85be9e527cb
---

# Context

Desktop and embedded hosts have different clocks and rendering rates. Tests
need to advance an animation without sleeping or waiting for physical time.

# Decision

Treat timing as an injected platform dependency. The engine receives monotonic
milliseconds through `now_ms`; each host supplies the clock. The core advances
animation from elapsed time. It does not sleep, read a system clock, or assume
that every frame takes the same amount of time.

Hosts own frame pacing. Interactive SDL uses its monotonic timer, the headless
host advances a simulated clock, and ESP32 uses `esp_timer_get_time`. Tests
control time explicitly.

# Approval basis

Jacob explicitly clarified, "yes timing is also a DI", during the 2026-10-07
session. This extends the platform boundary recorded in ADR-001.

# Consequences

- Tests can cover pause, elapsed time, and clock edge cases without delays.
- Animation speed is independent of the host's frame count.
- Host clocks must meet the monotonic-millisecond contract. The current engine
  defensively treats backward movement as zero elapsed time.
- Wall-clock dates, RTC integration, offline pet progression, and sleep recovery
  are not defined by this MVP. They need separate decisions when implemented.

# Alternatives

Calling a platform timer inside the core would hide a dependency and make
deterministic tests harder. Updating by a fixed amount per rendered frame would
make animation speed vary with rendering performance.

# References

- [Shared core and host adapters](adr-001-portable-c-core-and-host-adapters.md)
- [Clock and frame implementation](../../core/engine.c)
- [Fake host tests](../../tests/test_engine.c)
