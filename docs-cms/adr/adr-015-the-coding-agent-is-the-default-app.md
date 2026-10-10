---
title: The coding agent is the board's default app
status: Accepted
created: 2026-10-09T23:07:47Z
deciders: Michael Weingartner
tags: [agent, boot, esp32]
id: adr-015
project_id: jelligotchi
doc_uuid: 6a1a1257-4c9a-40cc-a8a7-66db671abab0
---

# Context

[ADR-013](adr-013-two-apps-on-one-board-with-flash-layout-a.md) put two apps on one
board: the pet in `factory` and Loki Agent, the coding agent of
[RFC-003](../rfcs/rfc-003-voice-agent-companion.md), in `ota_0`. The pet was the app
that started: flashing either app, `boot pet`, or an agent that failed to start all
led back to it, and the agent ran only after the pet's `app agent`.

On 2026-10-09 the user decided the opposite, in their words: "I want the coding agent
as the default." The board is now mainly the coding agent; the pet is a visit.

# Decision

The agent is the app that starts, whenever it can start:

- The pet, each time it starts, selects a `loki_agent` image in `ota_0` for the next
  reset, through the same trial as `app agent`. It does not restart: the pet runs
  until the next reset or power cycle, and the agent starts then.
- The exception is an agent whose last start failed: its trial was aborted (it reset
  before confirming) or it was marked invalid. Then the pet stays the boot app until
  a new agent is flashed, so a broken agent is not retried at every reset.
- `scripts/esp flash-agent` writes the agent, lets the pet start, and hands over
  through the pet's `app agent`, so a new agent starts at once and still on trial.
- `boot pet` (from the agent's console, or `scripts/esp boot PORT pet` from the
  host) is now a visit: the pet runs until the next reset.

ADR-013's layout, project-name checks and rollback are unchanged.

# Consequences

## Positive

- A power cycle, a crash after start-up, or a reset in the pet always lands in the
  agent.
- Flashing a new agent leaves it running, with the pet's trial still guarding it.
- A broken agent still falls back to the pet and stays out until it is replaced.

## Negative

- The pet cannot be the app across resets any more; playing with the pet for longer
  than one power cycle means choosing it again.
- The pet writes `otadata` once at each of its starts (and checks the agent's whole
  image, a fraction of a second).

## Neutral

- Boards without an agent in `ota_0` (such as a 1.75 that only runs the pet) behave
  as before.

# Alternatives Considered

## The pet restarts straight into the agent

Rejected: the pet could then never be used, since every visit would end at once.

## A setting in the pet for which app starts

Not needed for the decision as stated; it can be added if a pet-first mode is wanted
again.

# References

- [ADR-013: two apps on one board](adr-013-two-apps-on-one-board-with-flash-layout-a.md)
- [RFC-003: voice agent companion](../rfcs/rfc-003-voice-agent-companion.md)
- [Hardware guide: the voice agent app](../../docs/hardware.md)
