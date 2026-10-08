---
title: Healthy activities and action audit
author: Codex
created: 2026-10-08T03:46:24Z
tags: [assets, gameplay, memo, rendering, validation]
id: memo-015
project_id: jelligotchi
doc_uuid: 6e278fe7-8497-4121-9adc-96ab1e9ff82d
---

# Scope and Findings

The user requested healthy clickers, more fantastical celebrations, cached PNG
measurements, centered ring artwork, location labels, subtle gray backgrounds,
larger white/shadow headings, an audit of all actions, simpler icons, and a flash.
This records the working MVP, not approval of the broader draft architecture or
final game balance.

The audit found three substantive gaps: outing could succeed without changing
anything when already in the garden; tea and movie were identical play aliases;
and basic feed/care/play/clean/rest actions did not request an immediate desktop
save (periodic saves still existed). Outing now starts play at the garden; tea
and movie have distinct immediate need effects; every successful gameplay action
requests a save. Firmware remains explicitly unsaved because its storage adapter
is not implemented. Same-location travel, unchanged bedtime, and activating the
already-active pet return `not_ready` instead of pretending to change state.

# Complete UI Action Audit

All 28 UI action values and bottom/stat controls were traced through dispatch.
Navigation and configuration controls are identified separately: opening a menu
must not silently feed or alter a creature. Gameplay effects use the shared C game
command boundary, never a preview-only effect. Needs/bond are internally 0–1000;
the display shows need scores 1–100. Positive changes saturate at 1000.

| Action | Game path and observable internal effect |
| --- | --- |
| Feed / breakfast | FEED / MOMENT(0): eating activity for 50 ticks; consumes one food, satiety +300; useful first feed increments feeds and marks reward pending |
| Basic care | CARE: interrupts the current activity, wakes pet, floors low needs at 400, enters recovering/caring for 300 ticks, then well |
| Play | PLAY: playing for 80 ticks, then amusement +250 and energy -50 |
| Clean / wake | CLEAN: cleaning for 50 ticks then hygiene +350; while asleep the same control dispatches WAKE |
| Rest / wake | REST sets asleep and nap deadline; WAKE clears sleep and sets awake/schedule override deadlines; simulation recovers energy during sleep |
| Gift | GIFT: giving for 50 ticks, consumes one gift and bond +25 |
| Claim | CLAIM: requires pending/unclaimed reward and room for three food; adds food, clears pending, sets claimed; repeat claim rejected |
| Travel | TRAVEL toggles home/garden in pet.location; rejects asleep/busy or same target |
| Switch pet | ACTIVATE selects the other stable pet ID; rejects busy/resuming or current target; each pet retains independent internals |
| Bedtime +1h | BEDTIME updates the active pet's schedule, wrapping at 24 |
| Tea | MOMENT(1): starts play, social +60 and energy +100 immediately, then normal play result |
| Going out | MOMENT(2): starts play and moves to garden atomically; already in garden still receives the play activity/result |
| Movie | MOMENT(3): starts play and social +100 immediately, then normal play result |
| For now | Resolves injected local hour, or pet-phase fallback, to the same breakfast/tea/outing/movie command |
| Brush teeth | HEALTH(0): each accepted click adds hygiene +40 and bond +5 |
| Medicine / shot | HEALTH(1/2): each accepted click adds social +40 and bond +5; when unwell, starts existing recovery/floor treatment; later clicks continue during caring |
| Wash | HEALTH(3): each accepted click adds hygiene +70 and bond +5 |
| Stretch | HEALTH(4): each accepted click adds energy +40 and bond +5 |
| Care / collection / gifts / settings / home / moments / health | Navigation only; selects the corresponding ring, with no pet mutation |
| Select a healthy activity | Opens its target and resets a five-click round bound to the active stable pet ID; selection itself is navigation |
| Save | Requests the host's existing save operation; result reports saved/unavailable/failed; no artificial pet-stat change |
| MENU / CLOSE / BACK | Opens/closes or walks back one level; clicker → health → care → home |
| Tap stat tile | Advances the viewed need and restarts its slide timer; presentation only |

Five successful health clicks complete a round. Every accepted click calls game
logic before increasing progress or emitting positive feedback; extra clicks after
completion return not_ready and cannot grant another effect. Asleep, busy,
resuming, wrong-pet, and fully satisfied targets do not advance progress. A healthy
click is full only when both its target need and bond are already maxed (unless
medicine/shot can begin recovery). No medicine inventory, vaccine schedule, or
medical simulation is claimed. These are intentionally simple prototype rewards.
Delayed feed/play/clean/gift effects apply at their injected-time deadlines; a
successful press is not a claim that the delayed reward has already happened.

# Rendering and Asset Changes

The art inventory has 59 PNGs: 12 creature frames, 12 small icons, 13 menu icons,
5 meters, 5 health icons, 5 effect sprites, 2 backgrounds, 4 props, and one font.
All IDs are stable. Movie art has a square screen and closed clapper; detached
flourishes were removed from menu, outing, and moments symbols. Ring plates have
one thin neutral edge and a plain gray interior. Backgrounds use a light gray
center, quiet room/garden marks, and black edge vignette. A separate 16-gray
palette avoids tinting the scenery. The creature name is 3x the 8x12 font (36px,
50 percent larger than before); it and the 2x `@ HOME` / `@ GARDEN` line are white
with a two-pixel black drop shadow.

Build-time measurements include exclusive alpha bounds, the existing bottom-three-row
contact centroid, and full opaque-pixel centroid in both axes (Q8 pixel-center
coordinates). Generated immutable frame descriptors cache them. Actor state keeps
a borrowed pointer to the selected static descriptor plus screen origin/bounds.
Home locks contact to (233,256); healthy activities use (233,350) and place the
click target above the measured head. Rings center the creature at (233,233),
and each menu icon is centroid-centered within its button. There are no runtime
alpha scans for layout and no allocation in core rendering.

Celebrations now render masked 16x16 star/heart/orb/comet/wing sprites at 1x/2x,
with integer RGB565 fades during their final eight ticks. Rejected input retains
small square/cross feedback. The fixed pool remains 24 eight-byte particles;
style packs the sprite flag, type, and scale. Completion can fill the pool; the
burst-count zero setting still disables it. Damage includes previous/current
32px footprints, restoring the background before alpha blending so fades neither
accumulate nor leave trails. No blur or additional framebuffer is used.

Raw art payload is 108,864 bytes; 8,192 definition + 4,096 metadata allowances
produce 121,152 bytes within a proposed 131,072-byte pack ceiling (9,920 headroom).
These allowances are not a compiled content pack. The ESP32 still uses two
434,312-byte framebuffers (868,624 bytes combined before BSP/SDK buffers).

# ESP32 Acceleration Investigation

The [ESP-IDF 5.5 PPA documentation](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32p4/api-reference/peripherals/ppa.html)
describes dedicated BLEND and scale/rotate hardware on ESP32-P4. The pinned S3
soc_caps.h has no SOC_PPA_SUPPORTED or SOC_DMA2D_SUPPORTED capability; P4 defines
both. The [ESP32-S3 product documentation](https://www.espressif.com/en/products/socs/esp32-s3)
describes vector instructions, which are a possible CPU optimization, not a PPA.

The pinned LVGL 9.3 RGB565 software blender exposes NONE/NEON/HELIUM/CUSTOM paths,
with no S3-specific built-in sprite blend path. More importantly, our core already
composites the native RGB565 framebuffer before LVGL displays its canvas, so an
LVGL blender switch cannot accelerate the current particle loop. Keep the bounded
portable masked/integer blender. Future optimization can add a measured S3 SIMD
row implementation behind a port contract, with byte-for-byte golden pixel tests.
DMA display transport is not sprite alpha blending. No SDK/library upgrade or
claim of hardware-accelerated blending is part of this change.

# Validation and Remaining Polish

Validation/deployment evidence is appended after final checks. The initial new
tests caught invalid hand-constructed asleep fixtures (missing nap deadlines),
and the old particle damage bound expected tiny confetti rather than 32px sprites.
Fixtures now construct valid sleep state and the damage test checks the expanded
bound plus exact clean-frame restoration. The geometry test covers all 12 poses,
centroid asymmetry/padding, and empty masks. Static analysis caught an unnecessary
mutable game pointer in moment dispatch; it is now const.

Remaining polish: balance/cooldown rules for repeat health rounds, separate medicine
resources if desired, richer activity-specific actor poses, a production content
pack, measured S3 SIMD benefit, and physical screen/touch review. Saturated ordinary
feed/play/clean/gift still obey the existing prototype command rules; their activity
state changes even when a capped benefit cannot increase further. Browser preview
is artwork/interaction demonstration, not authoritative simulation. Existing saves
retain their schema because no new persistent pet fields were added.

# References

- [Assets and reproducible preview](../../assets/slice/README.md)
- [Core action tests](../../tests/test_game_actions.c)
- [Clicker and cached geometry tests](../../tests/test_pet_health.c)
- [External CLI acceptance](../../tools/debug/acceptance.py)
- [Earlier contact anchor evidence](memo-014-creature-contact-anchors.md)
# Deployment Evidence

The visual/gameplay image passed 19 desktop tests, 8 SDL-free core tests, all 19
sanitizer tests, C lint/size checks, and every pre-commit hook. The browser sheet
loaded all 59 PNGs and displayed 58 sprite cards. External acceptance passed
126 separate CLI calls locally, then 126 on USB, including all five clickers,
actual bond changes on each tap, completion rejection, PNG capture, reconnect,
tunables, and BACK hierarchy. An acceptance assumption was corrected: stretch
can raise energy above the nap auto-wake threshold, so sleep/wake is checked
before the health suite. Error names are the existing spaced protocol strings.

Flashed the discovered /dev/cu.usbmodem1101 device (303A:1001,
90:70:69:FE:21:DC), ESP32-S3 revision 0.2. The 725,264-byte image fits its 1 MiB
app partition with 323,312 bytes free. Binary SHA-256:
`7fb585bfdac9a6062fa8dc9d99452ba44a6a01b3557716b0ac64cc8a1f164677`.
ELF SHA-256: `27c60d11b745ea0fdc687cdfb13ad04b85ad3fbcdf52d1605268efab101f853c`.
Transfer hashes verified; monitor reset was USB_UART_CHIP_RESET. Version 0.1.1,
IDF 5.5.5, 8 MiB PSRAM test, CO5300 panel, CST9217 touch, LVGL, pet-ready and
debug-ready messages were observed. Monitor closed before USB acceptance.
Engine update samples were 50–52 Hz; a changed frame sampled 56 ms render and
100 ms presentation. These are not panel FPS or a formal worst-case benchmark.

Logs and captures are in ignored build/healthy-* and
build/acceptance-healthy-{local,esp32}/. Physical readability, physical taps,
and subjective animation quality remain unverified by the agent. The following
sound experiment is a separate increment and may supersede this image.
