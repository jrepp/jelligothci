---
title: Case button display blanking
author: Engineering Team
created: 2026-11-12T10:03:15Z
tags: [display, firmware, hardware]
id: memo-112
project_id: jelligotchi
doc_uuid: 1c3feb05-c6df-4532-943e-309a13fe2ff6
---

# Finding and change

On the Waveshare ESP32-S3-Touch-AMOLED-1.75, the case PWR button is separate
from BOOT. The vendor's [hardware reference](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/main/HARDWARE_REFERENCE.md)
identifies the conditioned PWR signal as active-high TCA9554 EXIO4. Existing
firmware did not handle it.

The [case button host](../../ports/esp32/main/case_button.c) samples that input
with a bounded I2C read. The [press state](../../ports/esp32/main/case_button_state.c)
debounces it, ignores long holds, and disarms on a failed read or an initially
held button. A short press blanks the display using the existing brightness
control; another restores the prior brightness. Touch gestures are suppressed
while blank, queued gestures are discarded, and the touch guard requires release
when transitioning. The engine continues running and pet time progresses. This
is display blanking, not MCU light sleep. PMIC power and charging settings are
unchanged.

# Evidence and limits

The [host test](../../tests/test_case_button.c) covers bounce, short and long
presses, startup held, and a failed read followed by rearming. The existing
[display trials](memo-050-display-sleep-and-brightness-validation.md) provide
historical brightness-restoration evidence. The current change still requires
physical verification of the case PWR button, visible blank and restoration,
and suppressed touch on the target board. No battery-current saving is claimed.