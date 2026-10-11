---
title: Main rebase and portable device firmware deployment
author: Engineering Team
created: 2026-10-11T01:40:25Z
tags: [deployment, firmware, integration]
id: memo-055
project_id: jelligotchi
doc_uuid: cf6dd7b2-90b8-4839-a0d4-ef3fb5c9ed76
---

# Overview

Rebased investigate/device-power onto origin/main at ba449b4 and flashed the
normal game profile, version 0.8.0, to SKU 31261. Serial identity was rediscovered
as 90:70:69:FE:21:DC at /dev/cu.usbmodem2101. The branch head at deployment was
fddc902 (subsequently re-signed as a495f9f); restored device work and the integration repairs remain uncommitted.
No remote branch was pushed.

# Rebase and integration

The pre-rebase branch is retained as backup/device-power-before-main-20261010.
Stash bdb567ef1d2021da18c2fb83684eec73096f4215 retains all original tracked and
untracked work, including its staged state. It was applied with --index after
the rebase and retained for recovery. 1Password failed twice with “failed to
fill whole buffer.” The rebase was restarted with command-only signing disabled;
the rewritten local commits were initially unsigned and commit.gpgsign remained true.

Studio conflicts retained main's shell, editor tools and read-only explanations
alongside the branch's Activities script and content. Asset conflicts retained
main's baby axolotl and hug frames plus the four location backgrounds. The new
baby form uses the adult axolotl activity-cost multipliers: energy 100 percent,
hydration 150 percent. This supplies the required explicit profile without a
new balance model.

The combined art needs 141,632 pixels and 17,704 mask bytes. Fixed desktop banks
now hold 147,456 pixels and 18,432 mask bytes, with 320 KiB staging. These changes
add 100 KiB to the single desktop startup allocation; no per-frame allocation
or ESP32 framebuffer growth was introduced. The oversized-pack test now derives
its input from the current capacity and still verifies that rejection preserves
the old pack. Firmware art remains compiled into flash.

The incoming Studio memo already used ID 043. The local device audit was renamed
to memo-054, preserving its UUID and creation date, and references were updated.

# Signing repair

After 1Password signing became available, all five branch commits were replayed
with SSH signing onto the same origin/main revision. The signed head is a495f9f.
Each signature verified against the configured public ED25519 key, fingerprint
SHA256:4Hs5zXJmkgbVJECWYCh5DsPIBfGlmzk6KwIg/67i3kA. Verification used an ignored,
command-scoped allowed-signers file; no permanent Git configuration changed.
The committed tree is identical to the pre-signing tree. Status and both staged
and unstaged patches matched their backups after restoring the pending work.
The old head remains at backup/device-power-before-signing-repair, with recovery
evidence under build/signing-repair and the temporary stash retained. No code
changed during signing, so the deployed firmware remains applicable. Nothing
was pushed.

# Deployment evidence

Image inspection reported version 0.8.0 and ESP-IDF v5.5.5. Application size was
1,813,088 bytes, with 57 percent of the 4 MiB app partition free. The image ELF
identifier was ba75a568daa375da239fcacde5c3483efd7611156c6b9a269bff0774c6db22ea.
The normal-profile isolation check passed before flashing.

make esp-flash wrote bootloader, application, partition table and OTA data;
every region passed hash verification. It did not erase NVS. The serial monitor
caused a USB_UART_CHIP_RESET, then captured version 0.8.0, the PSRAM memory-test
pass, CO5300/CST9217 initialization, checked brightness at 60 percent, session
startup and the debug-ready message. Save storage was available. RTC remained
unknown, correctly requiring synchronization.

Read-only debug requests reported profile game and board waveshare-31261. Display
health reported matching buffers, intact guards and heap, zero mismatches, zero
transfer failures and 1,292 bytes of main-task stack reserve at the sample. The
codec's channel-disable-before-enable diagnostic appeared, followed by successful
codec open and sound-ready output; this does not establish audible output.
No panic or spontaneous reboot was observed. The monitor and clients were closed.

Physical display/touch confirmation is pending. Serial success does not prove
image quality, touch coordinates, acoustic behavior or motion wake.

# Validation

The no-SDL suite passed 31/31. Desktop and sanitizer suites each passed 70/71 on
the first integration run: only the oversized-art fixture still assumed the old
bank size. After correcting that fixture, live-assets, full-pack decoding and
reload tests passed 3/3 in both builds. The normal ESP32 image built and passed
profile isolation. C formatting, clang-tidy, Cppcheck and size checks passed. Documentation
check and repair passed for 76 records with no repairs. The final diff has no
whitespace errors, and no serial client remains open.

Evidence is retained under ignored build/rebase-flash: pre-rebase diffs, stash
identity, resolved conflict backup, test/build/flash logs, serial boot log, debug
responses, image metadata and hashes, flashed binary and the final source diff.

# Pull request validation

PR #43 publishes the signed integration. Both complete local desktop and
ASan/UBSan suites subsequently passed 71/71, and all repository hooks passed
with the new files staged. The first CI run exposed missing DOM element types
in the activity editor and stale Studio UI fixtures. Explicit input, select,
button and canvas types fix the type errors without expanding the error baseline.
The UI fixture now pins the signed integration at 3d6bd08, including its required
location catalog. Accessibility snapshots are refreshed in the pinned Playwright
image and reviewed; screenshot references require the CI linux/amd64 renderer.
These Studio changes do not change the firmware already flashed.

# References

- [Portable device implementation](memo-053-portable-device-boundaries.md)
- [Device audit](memo-054-device-power-and-driver-audit.md)
- [Driver qualification](memo-052-imu-command-and-interrupt-qualification.md)
- [Asset budget](../adr/adr-012-data-driven-creature-species-and-presentation.md)
