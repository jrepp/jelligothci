---
id: memo-114
title: v0.19.0 release and attached-device deployment
author: Codex
created: 2026-11-12
tags: [deployment, esp32, release]
project_id: jelligotchi
doc_uuid: 9dc38446-f1c4-4762-95fa-8e0f5b4946dc
---

# Release

The owner requested the release cut and a flash of the latest firmware.
[PR #100](https://github.com/jrepp/jelligothci/pull/100) was squash-merged as
`9c796d6348be5875b53e61763e98d33d9a3abd0e`, creating game v0.19.0 and
Jelli Art v0.17.0. The exact candidate `847274fe6547d0e085f760ee5ee57208993cd6e5`
passed [release validation](https://github.com/jrepp/jelligothci/actions/runs/43200214495)
and [Studio validation](https://github.com/jrepp/jelligothci/actions/runs/43200218796).
The newer menu commit on main also had passing checks before this merge.

[Publication workflow](https://github.com/jrepp/jelligothci/actions/runs/43341459093)
completed successfully. Game validation, core tests on Linux/macOS/Windows,
Windows and macOS packaging, Studio tests/UI shards, and Studio image publication
passed. The workflow skipped firmware profiles; the local firmware build below
provides separate build evidence. The game release contains macOS and Windows
downloads, preview HTML/ZIP, editable artwork ZIP, source archive, and SHA256SUMS.
All six downloaded payloads matched SHA256SUMS. The downloaded source archive
was extracted and built outside the source checkout with SDL disabled; all
50 core/storage tests passed. No open PRs remained after publication.

# Firmware deployment

A separate checkout at `/private/tmp/jelli-release-019` built the release commit,
preserving the original dirty workspace. It reused the existing SDK/tool install
through a symlink, with fresh generated configuration and build output. Both the
wrapper-selected SDK and resolved SDK path matched the tracked ESP-IDF v5.5.5
commit. This was not a fresh toolchain bootstrap test.

Port discovery found one Espressif USB serial device at `/dev/cu.usbmodem1101`.
Before flashing, esptool backed up NVS at offset `0x9000`, length `0x6000`, to
the checkout's ignored `build/nvs-before-v0.19.0.bin`. Keep that backup private.
`make esp-build` passed; the application descriptor reported version 0.19.0,
with a 2,214,640-byte application image. The generated configuration enabled
the custom partition table and rollback support.

`make esp-flash PORT=/dev/cu.usbmodem1101` completed with transfer hashes verified.
The monitor observed a USB-initiated reset, version 0.19.0, passing PSRAM test,
panel creation, touch registration, pet-ready and debug-ready messages, and
`save=available`. No full-chip erase or eFuse changes were performed.

The startup log included the existing panel-format, I2C pull-up, and gesture
warnings. An I2S disable-before-enable error preceded successful codec/sound
initialization. Display checks reported matching buffers and intact guards.
Observed engine updates were 17–20 Hz; these are not panel FPS measurements.
The USB port disappeared after startup observation, without a captured panic;
the cause is unknown. The monitor was closed. Logs and build/image evidence
remain under the isolated checkout's ignored `build/` directory.

# Verification boundary

Build, flash transfer, and serial startup are verified separately. Physical
display, tap behavior, audio, and save contents await owner confirmation.
The USB disappearance limits further serial observation. No firmware changes
were made to address the startup warnings during this deployment.

# References

- [Hardware workflow](../../docs/hardware.md)
- [Release operations](memo-005-contributor-and-maintainer-handoff.md)
