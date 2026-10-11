# Board validation

This is maintained firmware, compiled only by the `power` or `factory` profiles.
`make esp-build` excludes it. The core engine has no experiment dependency.

| Layer | Owns | Reuse |
| --- | --- | --- |
| `experiments.h` | Host lifecycle, debug dispatch, input filter and wake observation hooks | Disabled hooks inline to no-ops |
| `boards/waveshare_31261.*` | SKU 31261 chip addresses, reset protocol, IRQ observation, read-only PMIC queries | Future board validation can call these probes |
| `power/power_diag.c` | Explicit bounded trial sequence, quiesce/restore, timing report | Development power profile |
| `boards/pmic_probe.*` | Bounded, read-only AXP2101 observations | Shared by power and factory profiles |
| `factory/` | Standalone suite, command dispatch and retained results | Shares `@J1`; no game, assets, sessions or networking |
| `network_probe.c` | Synchronized diagnostic wake counts | Validation builds only |
| `../../../../tools/debug/power_experiment.py` | Host capture and USB identity reconnect | Repeatable bench evidence |

Normal audio and disabled-network improvements live in board support, outside
this directory. Promoting a measured change means giving it normal host APIs and
validation; production code must not depend on an experiment implementation.

`./scripts/esp validate` selects `profiles/power.defaults`, stores configuration
and binaries under `build/esp32-power`, and compiles against the same SDK/BSP pins
as normal firmware. Firmware CI builds all three profiles and checks source/config
isolation. C formatting, Cppcheck and size gates include this directory.
Do not add an experiment that only builds when someone remembers to enable it.

The serial `power` identity reply includes schema 1, board `waveshare-31261`, and
profile `power`. The host recorder rejects other replies before invoking trials.
This declares the compiled board target; it does not identify unknown physical
hardware. A factory fixture must verify the board independently.
Keep result field meanings stable; incompatible changes require a new schema
and matching runner. Raw logs, JSON results and framebuffer evidence belong in
ignored build output; retain measured conclusions and limitations in memos.

The engine task owns command dispatch and trial execution. LVGL owns input
filter calls under its display lock. Probe IRQ handlers only count with ISR-safe
synchronization. Startup adds two I2C handles; the profile also owns a 4,072-byte
scratch game and 512-byte result buffer. No experiment task or frame buffer is
added. Disabled builds omit this storage and all probe initialization.

The shared portable `core/debug_protocol.c` and ESP32 `debug_usb.c` own parsing,
framing and transport for every profile. Only command handlers differ. The
factory capture tool uses the existing Python debug client and raw recorder.

The factory profile has its own entry point and reuses the PMIC probe without
booting a pet session. The [factory contract](../../../../docs/factory-validation.md)
defines the smoke suite, serial capture, pass meanings, limits and extension
pattern. It reports silicon identity and the ELF hash. New boards still need
revision manifests and fixture capabilities. Register each supported profile
in CI.
These power trials are not a manufacturing acceptance suite: visible display,
physical touch, audible output and current measurements require a fixture or
operator, and unsupported checks must never pass implicitly. Provisioning,
calibration writes, eFuses and erasure need separate explicit workflows.

See [the proposed validation architecture](../../../../docs-cms/adr/adr-013-board-validation-profiles.md)
and [the measured experiments](../../../../docs-cms/memos/memo-044-device-power-experiments.md).
