---
id: memo-019
title: Wi-Fi time sync and OTA foundation
author: Codex
created: 2026-10-09
tags: [debug, embedded, ota, time, validation, wifi]
project_id: jelligotchi
doc_uuid: 8f5eac5a-e0a0-4f84-90b0-b722c23d47e7
---

# Scope

The user requested Wi-Fi time sync and OTA, with initial setup through USB debug
and tweakables. They also requested a Sol subagent to plan a later Settings
button that opens an AP with a random password on the display, a browser settings
page, and optional Wi-Fi debug. That forward plan is [Draft RFC-003](../rfcs/rfc-003-wifi-provisioning-and-network-debug.md).
The AP/page/debug listener are not implemented by this increment.

# Implemented foundation

The ESP32 port owns station Wi-Fi, SNTP, timezone rules, and explicit HTTPS OTA.
The core only gains an optional host command callback and a larger bounded
request buffer. SDK dependencies remain in the port. New device tweakables stage
settings in RAM; apply validates and commits one versioned blob in the separate
`jelli_net` NVS namespace. Existing pet saves and creature tweakables retain their
formats. The password is omitted from replies, and the CLI can prompt without
placing it in shell history. Malformed saved settings disable network setup
without erasing the original bytes. This development build stores credentials
in unencrypted NVS.

The network task owns Wi-Fi configuration and flash downloads. SDK callbacks use
RTOS mailboxes. Only the engine thread mutates the pet, writes the RTC, or changes
the displayed offset. Capture pauses those mutations while the latest clock
sample waits in its single-entry mailbox. The mailbox carries monotonic receipt
time so a delayed sample remains current. NTP corrections leave simulation
elapsed time unchanged. UTC remains in the RTC; region presets calculate DST
without internet access. `manual` retains the prior numeric offset. Travel and
future timezone-law detection are not implemented.

The custom partition layout retains NVS and PHY offsets and adds two 4 MiB OTA
slots. OTA validates TLS certificates and hostnames and accepts only a direct
HTTP 200 response over HTTPS. The worker checks the completed image and project
name before selecting its slot. Downloads yield between 1024-byte writes, use
five-second HTTP I/O timeouts, and have a three-minute transfer deadline. SDK
DNS/connect work has its own timeout behavior; this is not a strict end-to-end
three-minute bound. Staging does not force a reboot. The explicit reboot command
checkpoints first; a failed checkpoint leaves the app running and sets a status
flag. Any reset after staging selects the new slot. Rollback confirmation waits
for rendering and readable save storage, not internet connectivity.

# Resource accounting

These are configured bounds, not measured device headroom:

| Resource | Budget |
| --- | --- |
| Existing engine and LVGL RGB565 surfaces in PSRAM | 868624 bytes, unchanged |
| Network worker stack | 8192 bytes, allocated once at startup |
| Request and status queues | One entry each, each type asserted at most 512 bytes |
| Time mailbox | One entry, two 64-bit values (16 bytes) |
| Encoded NVS settings | 456 bytes plus NVS overhead |
| Settings response scratch | 1472 static bytes on the engine thread |
| Debug request/reply buffers | 512 and 4096 bytes; total debug object asserted at most 4736 bytes |
| USB RX/TX rings | 1024 and 4096 bytes plus driver metadata |
| OTA transfer scratch | 1024 bytes on the worker stack |
| Flash app slots | Two slots of 4194304 bytes |

Worker and main state also hold fixed config/status copies. RTOS metadata,
Wi-Fi/lwIP buffers, and TLS allocations are additional. SDK allocations occur at
network initialization and during connection/update work; the firmware is not
heap-free. The portable core and frame path retain their allocation rules.
Network status exposes free/minimum internal heap and the worker stack's free
high-water mark for hardware acceptance. Peak SDK memory use and frame latency
under Wi-Fi/OTA load remain unmeasured.

# Validation and observed failures

The pinned ESP-IDF 5.5.5 firmware build passes with the new partition table and
rollback enabled. The application occupies about 1.55 MiB of each 4 MiB slot.
A separate generated config under ignored build output was used to preserve the
developer's prior generated SDK settings. [Hardware instructions](../../docs/hardware.md#wi-fi-time-sync-and-ota)
explain that migration path.

The first compile caught the old 4352-byte debug object budget. The larger
hex-encoded settings protocol needs a 512-byte request buffer, so its resource
assertion now accounts for that buffer, the unchanged reply buffer, and 128 bytes
of metadata. The USB RX ring was also enlarged to hold a complete request.
Formatting, static analysis, and complexity checks were used to keep the callback
and configuration code within the existing source limits. A mutable timeval
callback parameter has one local Cppcheck suppression because the SDK callback
type requires it; no analysis or size thresholds were relaxed.

The portable network config tests cover fixed encoding, malformed/truncated
storage, unsupported settings, bounds, missing credentials, insecure URLs,
protocol string encoding, and offset arithmetic across year boundaries. The
host callback test covers dispatch, long requests, overflow discard, and request
scrubbing. The supported timezone rules match the host timezone database for
every hour of 2026 and 2027, including DST transitions. This verifies the rules
on the host, not ESP32 libc or physical RTC behavior.

Core-only tests passed (16 tests). Full desktop and sanitizer runs did not pass:
existing `debug_local` runs printed their PASS message but CTest reported a
wall-clock timeout after thousands of seconds. A sanitizer `asset_reload` run
also timed out. Other tests passed, including the new tests. A repeated isolated
run and a run under macOS caffeinate showed the same debug timeout. The cause
remains unresolved; do not label the full suites green or weaken their timeouts.
Raw output is retained under ignored `build/network-*.log` paths.
After the final protocol changes, all six focused network/config/debug tests
passed in both desktop and sanitizer builds. `make lint-c` passed.
Documentation check and repair both passed without repairs.

# Remaining verification

No device was flashed, no credentials were configured, and no network connection
or OTA transfer was attempted on hardware. The new build needs one USB migration
before OTA can be used. Verify NVS preservation with a backup, first boot,
credential persistence and reconnect, RTC sync/offline retention, DST, live
heap/stack margins, valid and invalid image handling, interrupted download,
checkpoint-before-reboot, and rollback. Display/touch response during downloads
and RTC battery retention require physical checks.

The updater trusts the configured HTTPS server. It does not add release signing,
secure boot, anti-rollback version policy, a hosted binary service, or private
GitHub credentials. Future firmware must preserve save compatibility across
rollback. The first USB migration has no previous valid OTA slot to recover.

# References

- [Hardware setup and commands](../../docs/hardware.md#wi-fi-time-sync-and-ota)
- [AP setup and network debug plan](../rfcs/rfc-003-wifi-provisioning-and-network-debug.md)
- [Process lessons](memo-004-process-learnings-and-context-remediation.md)
- [ESP-IDF OTA](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/ota.html)
- [ESP-IDF system time](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/system_time.html)
