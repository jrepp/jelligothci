---
id: memo-030
title: Complete menu clarity audit
author: Codex
created: 2026-10-09
tags: [interface, usability, validation]
project_id: jelligotchi
doc_uuid: ff256894-09f3-427d-b757-29a938bef6bc
---

# Findings and implemented remedies

Most ring actions had names in their input descriptors, but rendered only icons.
Disabled icons became hard to identify. Bottom ring controls obscured long status
messages. The food restock action said MORE, medicine/shot disappeared during
cooldown, clock editing depended on recognizing a gear, and exercise failures
reported only NOT READY. These were discoverability problems on the round screen.

Ring controls now retain their 96-pixel touch targets and show icons with 2x
text labels on solid backgrounds. Long two-word labels wrap. Disabled text stays
readable. Page headings identify menus, Back/Close use words, and long feedback
moves above the lower buttons. Activities replaces the ambiguous Moments label.
The clock control says EDIT; its editor names Zone, Hour, and Minute. GET FOOD
replaces MORE, and uses two readable lines in the existing ninth cell.

Medicine/Shot remain visible and disabled during their existing one-hour cooldown,
with DOSE GIVEN - WAIT on tap. Specific exercise failures point to food, water,
or rest. Water at its cap says HYDRATION FULL. Sleeping activity views explicitly
say TOUCH PET TO WAKE. Pet/present action labels remain readable when disabled.
Locked Lilac taps show the growth hint; unlocked forms are still reversible.

# Page-by-page audit

| Page and every control | Outcome and explanation |
| --- | --- |
| Home: Menu, stat tile, pet, held/offered present | Menu opens navigation; tile cycles meters; pet responds or wakes; present opens Give/Put Away, offered present is caught. Existing touch behavior retained. |
| Root: Care, Activities, Gifts, Settings, Close | Visible labels distinguish navigation destinations; Close returns to the pet. Gifts opens the Presents grid. |
| Care: Feed, Basic Care, Clean, Play, Health, Water, Back | Feed opens food choices. Basic Care heals with a 30-second countdown. Clean and Play start timed actions. Health opens routines. Water fills hydration. Blocked actions retain names and return a reason. |
| Food: Meal, Fruit, Soup, Get Food, Back | Each food has readable effects and shares the displayed stock. Get Food refills five only when empty. Sleep/busy/empty hints explain feeding availability. Back returns to Care. |
| Activities: Breakfast, Tea, Going Out, Movie, Exercise, Back | Labels identify immediate activities; the suggested activity retains its gold ring. Exercise shortfalls name the next care action. Breakfast without inventory directs to Feed > Get Food. |
| Health: Brush Teeth, Medicine, Shot, Wash, Stretch, Back | Opens the matching routine. Medicine/Shot stay in stable positions during cooldown and explain why they cannot repeat yet. |
| Brushing: Brush Teeth, Floss, Brush, Mouthwash, Spit, Clean Up, Back | Current step uses a readable caption and progress dots. Accepted taps advance the bounded routine. Back leaves it; completion says Well Done. |
| Medicine, Shot, Bath, Stretch activity views: action button, pet, Back | Named step and progress dots; completed actions cannot award extra credit. Sleeping pet instruction describes touch-to-wake. Bath heading clarifies Wash. |
| Settings: Bedtime +1H, Pets, Rest/Wake, Edit, VOL −/+, Back | Labels distinguish schedule adjustment from sleeping now. Current bedtime/volume remain visible. Edit opens clock controls. Pets opens collection. |
| Clock: Zone −/+, Hour −/+, Min −/+, Back | Zone adjusts 30 minutes; Hour and Minute adjust their named units. Bounds report Time Zone Limit. Back returns to Settings. |
| Pets: nine authored slots, Back | Owned/active/new/locked badges, pet names, and tap-to-explore instruction retained. Locked slots open their authored unlock hint. |
| Pet detail: Bring Out/Active/Locked, Evolutions, Back | Readable disabled action text; larger unlock/switch guidance. Bring Out selects an owned pet; Evolutions inspects/selects its forms. |
| Evolutions: Mint, Lilac, Back | Tap-to-switch instruction. Owned active form has its badge; tapping locked Lilac shows Grow From Mint and authored growth time. Switching does not reset care or revoke unlocks. |
| Presents: nine cells, Back | Tap Owned to Hold instruction; unavailable selection says Play to Find Gifts. Owned selection continues through the existing held-present flow. |
| Present actions: Give, Put Away, Back | Give explains another-pet/sleep/busy restrictions. Put Away deselects without spending the item. Back only dismisses the panel. |
| Legacy More: Gift, Claim, Travel, Back | Audited and rendered, but not added to root navigation. These remain debug-accessible prototype actions. Missing reward/inventory messages are explicit; existing actions are unchanged. |

Small grid badges and secondary metadata still use 1x text where 2x would not
fit nine slots. Primary ring/action labels and essential instructions use 2x.
Hardware readability requires physical confirmation; captures alone do not prove it.

# Resources and validation

The renderer adds no dynamic allocation or new particle pool. UI and render-key
state each retain the last attempted slot in one byte for contextual feedback
and invalidation; ABI padding is handled by existing engine-size checks. Label
wrapping uses nine bytes of bounded stack storage; no save format changes.

Native-size screenshots cover all 17 page values plus the clock editor in
build/menu-audit.png. Navigation tests exercise sequential food/water/energy
exercise failures, visible-but-disabled medicine with no action, and full-water
feedback. The command-driven clock test uses the updated HOUR + label.

Host tests pass 42/42, core tests 21/21, and sanitizer tests 42/42. C formatting,
clang-tidy, Cppcheck, source-size checks, documentation validation, and the ESP32
build pass. Earlier checks caught the renamed HOUR + test client and two functions
over the complexity limit; the client was updated and rendering responsibilities
split before the successful runs. USB still enumerates, but the latest read-only
display command timed out. The DMA
fix and this menu revision have not been physically verified.

# References

- [Playing guide](../../docs/playing.md)
- [Recovery guidance](memo-029-recovery-guidance.md)
- [Display investigation](memo-024-esp32-display-mismatch-diagnostics.md)
