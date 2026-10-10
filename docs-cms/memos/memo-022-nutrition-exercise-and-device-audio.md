---
id: memo-022
title: Nutrition exercise and device audio investigation
author: Codex
created: 2026-10-09
tags: [audio, exercise, hardware, nutrition, validation]
project_id: jelligotchi
doc_uuid: e78c3668-7de8-423d-a401-ba218e3255c8
---

# Scope

The user requested more food types, hydration restored by water, a workout that
spends food and hydration, a barbell icon, separate feature commits, and flashing
after the planned collection/present work. They also reported silent audio.
Collection and present implementation is recorded in [memo-020](memo-020-pet-collection-and-present-controls.md).

# Nutrition and exercise

Hydration is a separate 0–1000 value; existing five-need indices remain stable.
Its fixed-point rate is one raw point per minute awake, quarter-rate asleep.
Water fills it without spending food. Meal, fruit, and soup share the existing
food inventory, have authored effects, and persist the selected food until eating
finishes. Exercise spends fullness, hydration, and energy atomically at start;
busy/repeated inputs cannot spend again. Completion grants play once. A resumed
workout retains the already-paid costs and deadline. Stored pets stay frozen.
The barbell animates using injected elapsed time. These are prototype balance
values, not approved long-term game economy decisions.

Save v5 adds hydration/remainder and v6 adds meal selection. Older records receive
70 hydration and the legacy meal selection. Nine-pet save payload is 3568/4096
bytes; JelliGame is 3992 bytes, JelliPet 392, and desktop JelliPetEngine 11760.
Hydration adds eight bytes to the event snapshot (64 to 72), so the fixed 32-event
log budget grows from 2064 to 2320 bytes. No new runtime allocation or task is added.
Two icons add 4352 raw asset bytes. The art pack ceiling is deliberately increased
from 160 to 161 KiB; this does not change C size limits.

# Audio observations before flashing

Rediscovered USB Serial/JTAG device serial 90:70:69:FE:21:DC at
/dev/cu.usbmodem2101. The running firmware accepted explicit chirp, hello, and coo
requests at volume 35. Each codec write completed with a submitted log and no
reported error; sound task stack watermark was 1884 bytes. A 35-second passive
capture also observed automatic coos at the configured 30-second interval.
Global coo settings were enabled; the pet was awake and idle with menus closed.
Normal automatic coos use volume 18 and menu taps use 25.

The user was away during the first probe, so the three sounds were repeated four
seconds apart. Listening confirmation is pending. Successful codec writes do not
prove physical speaker output. No audio configuration or driver was changed on
that evidence alone. Logs are in ignored build/audio-device-probe.log,
build/audio-device-repeat.log, and build/audio-idle-observation.log.

# Validation and deployment

Validation passed: desktop 40 tests, core-only 19 tests, sanitizer 40 tests,
clang-format, clang-tidy, Cppcheck, C size gates, and ESP32 compilation. A final
renderer regression checks changing barbell pixels from injected time and keeping
the pose fixed while paused. Its targeted exercise/render run passed (2 tests).
SDL CLI review opened the food grid, ate soup, filled hydration to 1000, and
started a workout showing the expected 150/200/50 resource deductions. Screenshots
of food, exercise, and hydration were inspected under build/. Documentation
validation and auto-fix passed; only tag sorting was repaired.

The firmware descriptor reports app version 0.2.0, with image size 1635616 bytes.
Source feature commits end at f79f7ba. The tested image includes the concurrent
uncommitted artwork refresh visible in this checkout; those unrelated files were
preserved and excluded from these feature commits. Image SHA-256 is
fdfab10cd3ef7e5ef961c9b9a954be1549247743de68643047ef04d1fb53da1a.
USB flashing completed with all transfer hashes verified. The monitor's
USB_UART_CHIP_RESET was initiated by the monitoring session, not an observed
panic. Startup reported the matching ELF hash, app 0.2.0, an 8 MiB PSRAM memory
test pass, CO5300 panel creation, CST9217 466×466 touch registration, NVS session
availability, ES8311 codec open, and debug ready. Idle samples measured 53–57 Hz.
RTC remains unknown pending time sync. Existing panel-init, I2C pull-up, and
single-point gesture warnings appeared alongside successful initialization; no
pin/driver change was made based on these generic warnings.

The loaded save retained pet 1 in Lilac form, present ownership/provenance,
timezone, and care values. Hydration migrated to 700. Pet count became three
because existing Movie Star discovery granted its companion. This is the intended
once-only legacy unlock migration. Firmware startup and flash logs are retained
under ignored build/nutrition-exercise-startup.log and
build/nutrition-exercise-flash.log. The pre-flash NVS backup is build/collection-nvs-before.bin;
its 24576-byte read completed before migration. Physical display/touch and sound
confirmation remain separate from compilation, flashing, and serial startup.

The on-device CLI exercised the nine-slot Pets view and food menu. Food actions
were correctly disabled with the user's empty inventory; no inventory cheat was
used. Water filled hydration to 1000. Exercise entered activity 6 and deducted
hydration to 800, then returned to idle after its deadline. Framebuffer captures
of Pets, Food, and the lifting barbell were inspected. This verifies renderer
output and normal input dispatch on firmware, not physical panel appearance or
finger touch. Results are in build/nutrition-device-acceptance.json. The user's
listening result for the repeated audio probe is still required to diagnose
physical output; audio is not claimed fixed.


Live present testing selected the owned Movie Star, opened its action panel, and
confirmed Give disabled for its original giver while Put away remained enabled.
Put away cleared the held selection and preserved the ownership mask (144).
The initial probe selected unowned Tea Sprite, which correctly left the gallery
unchanged; reading current button availability corrected the test selection.
The large action panel was inspected in build/device-present-action.png.
