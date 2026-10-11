---
title: Portable device boundaries and qualification reuse
author: Engineering Team
created: 2026-10-11T00:46:52Z
tags: [architecture, drivers, portability]
id: memo-053
project_id: jelligotchi
doc_uuid: 779fcf81-cdfa-462b-9dde-5be75cb75238
---

# Overview

Implemented the requested touch, display, RTC and IMU boundaries in the
device-power checkout. Existing validation profiles and station protocols remain
in place. The shared C modules use caller-owned storage and injected dependencies.

# Implementation

Touch fault cancellation and release gating now live in the portable core. SDL
uses the same guard for focus loss; a held contact is ignored until a release.
The pet engine accepts optional motion and display providers. SDL M publishes a
simulated motion sample; it does not change pet needs or wake the pet. Samples
are consumed once, cleared on failure, and rejected if timestamped in the future.
There is no required sensor in the normal game profile.

RTC decoding and writes moved from ESP32 session code into the portable PCF85063
driver. The host injects a clock provider and keeps durable initialization checks.
OS, STOP, test and 12-hour mode remain invalid. A failed or ambiguous STOP/date
write now attempts restart while still reporting failure and withholding trust.

The QMI8658 qualification runner now uses a portable protocol implementation.
Single-byte reads, explicit INT2 gating, bounded done/ACK checks and WoM exit
are preserved. Operation and ACK errors are available independently. Restoration
also checks that command-done clears; a visible register match alone cannot hide
a pending command. A failed cleanup leaves the changed-state flag latched.

Display brightness uses the driver's checked transmission result. The host owns
requested percent and validity; normal startup requests 60 percent. Both panel
and power validation restoration use this state, avoiding the BSP's stale or
rounded cache. The display mutex, canvas and framebuffer ownership stay intact.

# Resource boundaries

No new task, queue, framebuffer or per-frame allocation is introduced. The
single-event motion mailbox is capped at 32 bytes. Register transport holds five
pointers; the QMI state remains within the existing 80-byte qualification budget.
The register adapter uses a 17-byte write buffer and a 20 ms transfer timeout.
RTC decoding reads 11 bytes. QMI polls at most 40 reads per done/ACK phase and
uses injected 5 ms waits plus a 100 ms cleanup settle when WoM was attempted.
These operations belong outside the frame callback. Desktop pet engine size is
12,056 bytes, within the existing 32 KiB assertion; this is not total firmware RAM.

# Verification and findings

Desktop CTest passed 71/71 and the no-SDL suite passed 31/31 on macOS/AppleClang.
The sanitizer suite passed all tests except a preview-cache assertion while source
files were still changing. With sources stable, the preview test and all four
changed C test targets passed under sanitizers (5/5). No sanitizer memory error
was reported. The extended IMU tests sweep every setup and cleanup I/O failure
point, including ambiguous writes and command-done stuck after ACK.

Normal, power and factory firmware built successfully and passed profile isolation.
C formatting, clang-tidy, Cppcheck and size checks passed, including explicit checks
for untracked new sources. Size-gate self-tests passed 5/5; incremental-version
validation and pre-commit configuration validation also passed. Documentation
check and repair passed; repairs only sorted the new records' tags.

No device was flashed during this change. Earlier physical observations do not validate this
new binary; pickup, wake reliability, current and battery life remain unverified.

Initial checks caught an omitted drivers directory in the art preview staging
list. Both preview staging and the incremental-version fixture now copy it.
Static analysis found implicit multiplication widening and implicit memcmp
conditions in extracted code; explicit types and comparisons fixed them. Initial
complexity failures were resolved by separating baseline validation and brightness
state updates, without changing limits. Supplemental checks cover the new,
untracked C files without modifying the user's staging area.

# References

- [Interface design](../adr/adr-014-injected-device-services.md)
- [Porting guide](../../docs/development.md#device-services)
- [IMU qualification](memo-052-imu-command-and-interrupt-qualification.md)
- [RTC trust](memo-051-rtc-progression-validation.md)
- [Display evidence](memo-050-display-sleep-and-brightness-validation.md)
- [Touch evidence](memo-049-touch-recovery-and-scheduling-validation.md)