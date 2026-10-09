---
title: Two apps on one board with flash layout A
status: Accepted
created: 2026-10-09T14:37:39Z
deciders: Michael Weingartner
tags: [agent, architecture, embedded, esp32, flash]
id: adr-013
project_id: jelligotchi
doc_uuid: 06a0ff27-45ba-424c-b690-934405bdbc44
---

# Context

[RFC-003](../rfcs/rfc-003-voice-agent-companion.md) proposes a voice agent app
for the same Waveshare boards. On 2026-10-09 the user chose to keep the pet and the
agent on the same board and switch between them, rather than dedicate a board to
the agent. The two cannot share one image. The pet is 5.46 MB with its embedded
artwork ([ADR-012](adr-012-high-resolution-asset-density-and-flash-partition.md)).
The agent needs a different configuration (240 MHz, Wi-Fi, TLS) and network-facing
code that should never run inside the pet. The pet's save lives in `nvs` at 0x9000
and must survive the change.

ESP-IDF's bootloader can start either a factory app or an OTA slot, chosen through
`otadata`. With app rollback enabled, it returns to the previous app when a newly
selected one resets before confirming that it started.

# Decision

**Layout A.** `ports/esp32/partitions.csv` serves both boards and both apps
(16 MB; sizes in binary megabytes):

| Name | Type | Offset | Size | Use |
| --- | --- | --- | --- | --- |
| `nvs` | data/nvs | 0x9000 | 24 KB | Pet save; unchanged |
| `phy_init` | data/phy | 0xF000 | 4 KB | Unchanged |
| `factory` | app | 0x10000 | 7 MB | Pet (8 MB under ADR-012) |
| `ota_0` | app | 0x710000 | 4 MB | Agent |
| `otadata` | data/ota | 0xB10000 | 8 KB | Which app starts |
| `agent_nvs` | data/nvs | 0xB12000 | 56 KB | Agent settings, apart from the save |
| `model` | data/spiffs | 0xB20000 | 1 MB | Reserved for ESP-SR models |
| `art` | data/0x40 | 0xC20000 | 3,968 KB | Reserved for agent artwork |

**Two projects.** `ports/esp32` (project `jelligotchi`) and `ports/esp32-agent`
(project `jelli_agent`) share the board wrappers
([ADR-011](adr-011-selectable-esp32-board-variants.md)) and this table. Only the
pet's flash writes the bootloader, the table, and `otadata`, so a full pet flash
always returns the board to the pet. `scripts/esp flash-agent` also resets the boot
choice to the pet, then writes the agent to `ota_0` by name with ESP-IDF's
parttool, so a new agent image is only ever entered through the trial below.
Parttool reads the table from the board, so on a board without layout A it fails
instead of overwriting the pet.

**Switching.** Each app boots only the other's project name, because `ota_0` can
hold a stale vendor app from the board's factory flash. The pet's `app agent` debug
command checkpoints the game, selects `ota_0` with `esp_ota_set_boot_partition`
(which verifies the whole image), and restarts once the reply is sent. The agent's
`boot pet` console command selects `factory`, which erases `otadata`.
`scripts/esp boot PORT pet|agent` switches from the host with otatool, for recovery.

**Rollback.** Both projects set `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, and both
refuse to build with a generated configuration that has it off. A switch from the
pet starts the agent on trial. The agent confirms itself once its start-up has
finished and before its console takes commands, so a "boot pet" request cannot race
the confirmation. The confirmation means start-up survived and the console works,
not that the display works: an agent with a failed display stays selected and
returns through its console or the host switch. A reset before the confirmation
returns the board to the pet. The host switch writes no trial state, so it skips
this protection.

**Settings.** The on-device switch in Settings that the user asked for arrives with
the agent MVP (RFC-003 phase 1). Phase 0 switches through the developer paths above.

# Consequences

## Positive

- The save and the pet survive: `nvs` is untouched and any full pet flash makes
  the pet the app that starts.
- A broken agent cannot strand the board in normal use: it rolls back to the pet,
  and the host can always choose the pet with otatool or a full pet flash.
- The agent's network code never runs inside the pet, and each app keeps its own
  configuration and dependency locks.

## Negative

- The pet's slot shrinks from 8 MB to 7 MB, leaving 22% free at 5.46 MB. New pets
  must fit; growing past 7 MB means a new layout and re-flashing both apps.
- The table, the project names, and the rollback setting must agree across two
  projects; `tests/test_partitions.py` pins them.
- The pet's build warns that its image would not fit `ota_0`. That is expected: the
  pet never goes there.
- The host switch skips the agent's trial, and a reset through it skips the pet's
  last checkpoint (up to a minute of progress).

## Neutral

- Existing checkouts regenerate their `sdkconfig` once to pick up rollback.
- The 32 MB 1.75C could later use a layout B with `ota_1` for updates from the Mac.

# Alternatives Considered

## A board dedicated to the agent

Simpler (no switching, no repartition), but the user chose the shared board.

## One firmware with a pet mode and an agent mode

Rejected in RFC-003: the combined image would not fit 8 MB, the configurations
conflict, and the pet would carry the network attack surface.

## Keep the pet's 8 MB slot

Layout A with an 8 MB `factory` still fits, but leaves 2.9 MB for agent artwork
instead of 3.9 MB. RFC-003 budgets 6 to 8 large poses (about 0.45 MB each with
alpha) plus overlays, while the pet has 1.5 MB of headroom in 7 MB.

# References

- [RFC-003: Voice agent companion](../rfcs/rfc-003-voice-agent-companion.md)
- [ADR-012: High-resolution asset density and flash partition](adr-012-high-resolution-asset-density-and-flash-partition.md)
- [memo-024: Phase 0 two-app foundation and hardware probes](../memos/memo-024-agent-phase0-two-apps-and-hardware-probes.md)
- [Hardware guide](../../docs/hardware.md#partition-table-and-regenerating-sdkconfig)
- [ESP-IDF app rollback](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html#app-rollback)
