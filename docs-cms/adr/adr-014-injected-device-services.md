---
title: Injected device services and portable register drivers
status: Proposed
deciders: Engineering Team
created: 2026-10-11T00:46:52Z
tags: [architecture, drivers, portability]
id: adr-014
project_id: jelligotchi
doc_uuid: 17263f2f-3ede-49e7-a3ac-3d22b05c3b14
---

# Context

The user requested engine-facing changes from the device qualification work,
including IMU, touch, display and RTC support in the device-power checkout.
Similar boards and the desktop host need to share behavior without sharing SDK
headers. This records the implemented design as a proposal for review.

# Decision

Use small independent C interfaces for clock, motion and display brightness.
Each carries its own context; the host owns its lifetime and synchronization.
A missing callback reports unavailable. Native positive errors cross the seam;
negative errors name portable failure conditions. The original JelliPlatform
contract stays compatible with existing hosts and positional initializers.

Bind optional motion and display providers to JelliPetEngine after initialization.
Poll motion once per frame through a nonblocking callback. The sample is transient,
not a saved game field or a pet action. A mailbox coalesces bursts and consumes an
event once. The SDL host can inject a simulated event with M. Future sensor hosts
must acquire data outside this callback and use host synchronization to publish it.

Keep RTC sampling and persistent clock trust in host session code. Inject the
clock into the ESP32 session instead of opening I2C there. A healthy oscillator
alone never authorizes offline progression. Persistently revoke trust before
writing the clock and grant it again only after a successful write and commit.

Put portable QMI8658 and PCF85063 protocols in drivers, a separate CMake library
that does not depend on the game or an SDK. Inject register transfers, monotonic
time and delays. Chip drivers own no bus, pin, task or heap storage. The ESP32
adapter owns the address and bounded I2C transfers. The QMI qualification runner
uses the shared protocol, with its existing explicit board/firmware gate.

Move the touch cancellation/release gate into the portable core. Apply it before
converting host samples to gesture events. Both ESP32 and SDL use this policy.
Use checked display brightness writes and retain requested percent in the host.
Validation restoration reads this state instead of the lossy BSP brightness cache.

# Consequences

The desktop tests can exercise chip protocols and inject faults with fake time.
The game can run with absent sensors, without enabling them at startup. Register
protocols may block during bounded setup/cleanup; they must run in host task or
validation context, never in engine frame or ISR callbacks. The motion mailbox
is not itself thread safe.

No automatic screen-sleep policy, motion-based game reaction or IMU wake source
is enabled. Ordinary panel sleep and INT2 pickup still have the qualification
limits recorded in the preceding memos. The active QMI driver remains excluded
from normal and factory firmware; factory inventory stays read-only.

# Alternatives Considered

A single universal board object would couple independent services and make
missing devices harder to model. Keeping the protocols inside ESP-IDF would
prevent deterministic host tests. Moving synchronous setup into the engine's
poll callback would violate the existing frame-time boundary.

# References

- [Device interfaces](../../include/jelli/device.h)
- [Register transport](../../include/jelli/drivers/register_io.h)
- [Portable host boundary](adr-001-portable-c-core-and-host-adapters.md)
- [Qualification limits](../memos/memo-052-imu-command-and-interrupt-qualification.md)
- [Implementation findings](../memos/memo-053-portable-device-boundaries.md)