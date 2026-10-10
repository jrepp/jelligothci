# Playing and debugging

Run commands from the repository root unless stated otherwise.

## Desktop

Prerequisites: a C compiler, CMake 3.21+, Ninja, SDL2 2.0.18+, and Make (optional).
SDL and ESP32 builds embed the PNG assets through repository-local uv, Python
3.12, and Pillow 12.0.0; first use needs Bash, curl, and network access. The
SDL-free C core build still needs only CMake and a compiler.

```sh
# macOS
brew install cmake ninja sdl2
# Debian/Ubuntu
# sudo apt install build-essential cmake ninja-build libsdl2-dev

make run          # pet; saves to build/pet-save.0 and .1
make run-shapes   # original color/motion diagnostic
make test
make sanitize     # address + undefined behavior sanitizers
make core-test    # no SDL dependency
```

GitHub Actions builds and runs the core tests on Linux, macOS, and Windows,
with SDL disabled. This checks host portability independently of the SDL and
ESP32 ports. A separate Linux job runs the SDL smoke test and sanitizers.


## Debug CLI

The desktop app and ESP32 share the same state, button, tap, and screenshot
protocol. On macOS/Linux, `make run` opens `build/jelli-debug.sock`; restart an
already-running app to enable the new interface. From a second terminal:

```sh
./scripts/jelli-debug state
./scripts/jelli-debug buttons
./scripts/jelli-debug press MENU
./scripts/jelli-debug press CARE
./scripts/jelli-debug press "BASIC CARE"
./scripts/jelli-debug screenshot build/local-screen.png
```

Without `--port`, the CLI defaults to this checkout's local socket. For another
instance, launch the executable with `--debug-socket /path/to/session.sock` and
pass `--socket /path/to/session.sock` to the CLI. Direct executable runs without
`--debug-socket` do not open an endpoint. With `--headless --debug-socket PATH`,
the app stays alive until stopped (or `--frames N` completes), pacing iterations
so external clients can interact; ordinary headless tests retain their fast mode.
The local endpoint is a user-only Unix socket, not a TCP listener.

Normal exit removes the socket. After a crash, confirm no game owns the endpoint
before removing its stale socket or choose a fresh path. Startup never replaces
an existing socket/file. Client disconnect releases an unfinished capture; closing
the window or pressing Escape still works during capture. Local state, input,
screenshots, and reconnect behavior are tested against the actual SDL app.

The ESP32 build includes a small debug interface on its existing USB Serial/JTAG
console. Build with `make esp-build`; when ready to deploy, discover the port
with `./scripts/esp ports` and use the separate `make esp-flash PORT=...` command.
Close `esp-monitor` and other serial clients before using the CLI. Device state,
commands, and framebuffer screenshots have been exercised over USB. A screenshot
reads the engine buffer; it cannot verify the pixels shown by the physical panel.

```sh
./scripts/jelli-debug ports
# Set this to the port discovered above, rather than reusing an old device path.
export JELLI_DEBUG_PORT=/dev/cu.usbmodem...  # Linux typically /dev/ttyACM...
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" state
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" buttons
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" press MENU
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" press CARE
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" press "BASIC CARE"
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" tap 114 332
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" screenshot build/pet-live.png
```

For an ESP32 display mismatch, `display` reports the latest five-second sample of
engine/canvas equality, boundary guards, heap integrity, panel transfer counts,
and minimum remaining
engine-task stack bytes. `checks: 0` means no sample yet. `mismatches` counts
unequal samples, not frames. These checks do not read panel memory.

```sh
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" display
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" display refresh
```

`display refresh` queues a full retransmission of the existing LVGL canvas on
the next uncaptured frame. It does not reset the device, change pet state, or
repair a differing canvas. Capture the screenshot and diagnostics before using
it, then compare the physical panel. The two full PSRAM frames have 32-byte
boundary guards on each end; invalid copy bounds or damaged guards stop the
firmware with an error instead of continuing an unsafe copy.

The CLI runs through repository-local uv, Python 3.12, and pinned pyserial 3.5.
`state` reports the last rendered view and visible buttons. `press` accepts a
current label or one-based index; it checks that the page has not changed.
Inputs use the normal care rules. A delivered input can still be rejected by the
game; inspect the subsequent state's result field. The CLI never automatically
repeats a timed-out input.

Screenshots contain exact renderer pixels plus matching `.png.json` state.
Capture temporarily freezes the game to reuse its existing framebuffer, then
releases it; physical taps during capture are ignored. Abandoned captures expire
after five idle seconds, with a thirty-second hard limit. Capture time is forgiven.
No extra framebuffer or task is added; adapter static state is about 4.2 KiB,
plus 4.25 KiB of startup USB rings and SDK overhead. Ordinary state reads do not
freeze the game. See [the protocol and validation record](../docs-cms/memos/memo-009-wired-debug-interface-and-validation.md).


## Large ring controls and moments

Tap MENU for care, moments, gifts, and settings. Pet switching and sleep/wake live
in Settings.
Only useful actions occupy ring positions. The bottom arrow BACK is the only
return action; X CLOSE dismisses the home ring. There is no separate confirm or
save button: successful actions request a save. The home tile shows one stat at a
time: Mood, Food, Energy, Hygiene, Play, Social, Bond, or Sleep. Swipe sideways
to choose. Completed activities briefly count up each improved stat in sequence;
passive changes and debug cheats do not trigger that celebration.
Gentle touches improve mood; rapid repeated touches request space and then show
an overwhelmed reaction. Sensitivity recovers with time. Each pet has stable
moment preferences and time-of-day bonuses; current balance is a prototype.

Night fades over 12 seconds to darker gray with a crescent above the actor;
icons gain a subtle cool tint. Sleeping pets emit medium-blue Z particles from
half the visible sprite height above the cached centroid.

MOMENTS offers breakfast, tea, going out, and a movie. The prototype maps these
to existing feed, play, and travel actions. Both ports use UTC plus the configured
local offset for suggestions; unknown clocks fall back to the pet relative day. Suggestions
never lock actions. Distinct ritual scenes and effects are tracked in
[PRD-001](../docs-cms/prd/prd-001-moments-and-readable-care.md).

Button presses emit short native confetti bursts, including while gameplay is
paused. The fixed pool contains 24 eight-byte particles and uses no extra
framebuffer or runtime allocation. The rings/art/particles were flashed and externally tested; see
[memo-012](../docs-cms/memos/memo-012-cli-acceptance-and-creature-tunables.md) for current deployment and tuning evidence.


## Runtime tunables and external acceptance

The same CLI supports SDL and the ESP32. Add `--port PORT` before the command for
USB; omit it for the default local socket. Get stable creature IDs from `state`.

```sh
./scripts/jelli-debug tunables
./scripts/jelli-debug tune idle_frame_ms 900
./scripts/jelli-debug tune animation_scale_pct 300
./scripts/jelli-debug tunables --creature 1
./scripts/jelli-debug tune idle_frame_ms 1200 --creature 1
./scripts/jelli-debug tune idle_frame_ms reset --creature 1
```

`animation_scale_pct` scales UI durations: larger means slower. 300 makes slides,
carousel holds, and confetti three times as long as the original defaults.
Creature idle timing is independent: 900 ms/frame is twice the original duration.
The listing includes ranges, units, defaults, effective values, and their source.
Other controls are `stat_hold_ms`, `stat_slide_ms`, `burst_count`, and
`particle_spread_pct`. Hold/slide values are before duration scaling. Tunables
are session-only; reset removes the selected override and restores inheritance.
Gameplay timing and rewards are unaffected.

The portable [registry](../include/jelli/tunables.h) supports immutable form profiles
through `jelli_tunables_register(&engine.ui.tunables, profiles, count)`. Profiles
must outlive the engine. Resolution is default → form profile → global override
→ creature override. Both existing forms currently inherit common defaults.
This establishes a content extension point without inventing different balance
for the two creatures.

For a black-box acceptance run against an already-running game:

```sh
python3 tools/debug/acceptance.py --socket build/jelli-debug.sock --tunables --output build/acceptance-local
python3 tools/debug/acceptance.py --port "$JELLI_DEBUG_PORT" --tunables --output build/acceptance-esp32
```

Run local first, then discover the USB port and run against firmware with the
same protocol extension. The harness uses separate public CLI processes, writes
JSON evidence and a verified PNG, tests sleep/wake and menus, and restores sleep
state on success. Close other serial monitors. On the verified macOS native USB
Serial/JTAG connection the CLI keeps DTR/RTS asserted to avoid reset on open;
other serial adapters need separate validation.

### Healthy activity debug flow

With a running local debug socket (or add `--port` for the ESP32), start at the
closed home scene:

```sh
./scripts/jelli-debug press MENU
./scripts/jelli-debug press CARE
./scripts/jelli-debug press HEALTH
./scripts/jelli-debug press 'BRUSH TEETH'
./scripts/jelli-debug press 'BRUSH TEETH'  # one game-logic tap; inspect current stage and target
./scripts/jelli-debug state              # visual.clicker_hits, needs, bond, result
```

The activity icon sits above the actor; touching the actor itself does not count.
BACK walks activity → health → care → home. On the health ring use button `0` to
select the bottom BACK unambiguously (the ring also contains BACK).
The manual external acceptance script supports `--health` alongside `--tunables`;
it checks every clicker, internal bond changes, completion guards, and screenshots.

### Sound audition

Nine tiny procedural cues are available through `jelli-debug sound`: `chirp`,
`happy`, `sparkle`, `hello`, `sleepy`, `tap`, `coo`, `confirm`, and `pet`. Hello and sleepy are vowel-like pet voices.
Menu confirmation, Back/Close, and pet interaction use distinct cues, coalesced
within 120 ms and scaled by the saved master volume.
Pass `--volume 0..80` (default 35). Both native SDL and the ESP32 support this
command; sound output is asynchronous and does not block the engine thread.
Interactive SDL initializes audio at startup; headless SDL needs `--audio`.
`SDL_AUDIODRIVER=dummy` enables silent integration checks. No sound plays on boot.

```sh
./scripts/jelli-debug sound hello --volume 35
python3 tools/audio/preview.py
open build/audio/preview.html
```

See [sound sources and audition exports](../assets/sound/README.md). ESP32 initialization
uses the pinned BSP's ES8311 codec path. Its codec/driver allocate at startup;
steady-state synthesis uses fixed buffers and a four-request queue.

### Readable event companion and activity routines

```sh
./scripts/jelli-viewer --socket build/jelli-debug.sock
# Or use the discovered USB port (close other serial clients first):
./scripts/jelli-viewer --port "$JELLI_DEBUG_PORT"
```

Open `http://127.0.0.1:8765`. The companion owns the debug wire, polls at 800 ms,
groups repeated taps, and shows accepted/blocked actions and actual need changes.
Pause reading or scroll back to hold the feed still. Care details and game buttons
are collapsible. There are no frame/animation/decay events. The device retains
32 fixed 64-byte records; the browser keeps 100 grouped rows and reports history gaps.
`jelli-debug events --after 0` also reads the bounded stream directly.

Brushing is a six-step routine: brush, floss, quick brush, mouthwash, spit, clean up.
The first brush and floss vary from one to three taps; later steps default to one.
Other healthy activities default to three taps. The icon, label, and dots update
at each step; every accepted tap affects care/bond, with a final celebration.
Tuning applies when a round starts, so an in-progress target stays fixed.

```sh
./scripts/jelli-debug tunables
./scripts/jelli-debug tune activity.taps 3
./scripts/jelli-debug tune activity.brush.min_taps 1
./scripts/jelli-debug tune activity.brush.max_taps 3
./scripts/jelli-debug tune activity.floss.min_taps 2 --creature 1
./scripts/jelli-debug tune activity.floss.max_taps 3 --creature 1
./scripts/jelli-debug tune activity.finish.taps 1
./scripts/jelli-debug tune scene.night_transition_ms 12000
```

All canonical names have a namespace: `pet.*`, `ui.*`, `fx.*`, `scene.*`, or
`activity.*`. Legacy flat input names remain aliases; listings return canonical
names. Overrides remain session-only, with creature overrides above global/form
profiles. Tap counts are bounded 1–6. Effective min/max endpoints are sorted,
including mixed-scope overrides. The old `ui.stat_hold_ms`/`ui.stat_slide_ms`
settings are retained for compatibility but the single mood tile does not slide.

The debug viewer includes an active-pet cheat panel: needs and bond accept 0–100,
food and gifts 0–20, and Heal restores care needs and clears illness/touch stress.
Changes appear as Debug events and obey active recovery/item reservations.
CLI examples: `./scripts/jelli-debug cheat energy 75` and
`./scripts/jelli-debug cheat heal` (add the usual socket or port option).
Quiet periodic coos use `audio.coo_enabled` and `audio.coo_interval_ms` (default
30000); menus, sleep, activity, and recent input suppress them.

Windows release builds compile pinned SDL2 with MSVC and bundle `SDL2.dll` beside
the executable. Extract the ZIP before running it. Builds are unsigned; the live
Unix-domain debug socket is currently macOS/Linux only. Windows still tests the
portable debug protocol, saves, rendering, and headless SDL game. For a local
Windows SDL build, install the pinned uv from `toolchain.env`, build/install SDL2,
and configure CMake with its install directory in `CMAKE_PREFIX_PATH`.

Settings now owns pet selection, sleep/wake, bedtime, and a large local/pet clock.
Back and close use icon-only controls partly below the round screen. Ring icons
travel out/in on menu changes; CLI presses wait up to three seconds for the ring
to settle. Shots take 1–3 taps, medicine one small dose; each has a per-pet hour
cooldown. Shot progress survives menu changes. The desktop checkpoint codec now
writes version 2, migrates version 1, and rejects future versions without replacing
them. Device checkpoints and portable pet-memory import are still planned.

Touch navigation: swipe up to open the menu; swipe down to go back one level
(or close the main ring). Swipe left/right on the main scene to select Mood,
Fullness, Energy, Hygiene, Play, or Social. The selected stat stays put. Desktop
mouse drags use the same recognizer: taps commit on release, swipes need 48 pixels
and a clear dominant axis, and ambiguous drags do nothing.
`./scripts/jelli-debug --socket build/pet.sock swipe left` exercises the same UI
navigation over the debug interface; `up`, `down`, and `right` are also supported.

Settings shows a smaller clock face with a gear button. Tap the gear for distinct
TZ −/+, HR −/+, and MIN −/+ controls; Back returns to Settings. TZ changes a
persisted offset relative to UTC (or the simulated pet clock when time is unknown)
in 30-minute steps, bounded to −12/+14 hours. Hour and minute adjustments
wrap at midnight. These affect the display, atmosphere, and moment suggestions;
they persist with the game checkpoint. Use the debug clock sync to set the RTC. The sleep button
has an ON/OFF badge showing actual sleep state. Menu movement uses a short
acceleration followed by a long integer ease-out, with no extra frame buffers.

Timed activity buttons carry a play badge and return to the pet scene when
started. Unavailable actions are dimmed using the same game rules as execution;
they produce no celebration or state change. The debug console also disables
those buttons. Settings shows the VERSION-derived build number in large text.



## Linked sleep, habits, and checkpoints

Settings REST starts both your bedtime record and the pet's sleep; WAKE ends both.
Manual bedtime stays active until you wake it. Automatic pet naps do not create
personal sleep records. The last eight sessions and cumulative sleep duration are
stored, with known wall timestamps distinguished from simulation-only durations.
Pet selection is blocked during a linked sleep session.

The separate Sleep score uses a rolling 24-hour history, with eight hours as the
prototype target. Above 75%, positive Social and Play gains receive a 20% bonus;
below 25%, they receive 20% less and awake energy drains faster. Unknown history
starts with a provisional 75% score that fades out as a day of data arrives; it
is not recorded as actual sleep. Hourly bins also track meals and play duration,
with weighted expiry and separate lifetime totals. Food/play history targets are
three meals and ten minutes of play; these are prototype balance values.

Sleep regenerates energy and preserves play/social needs. Food slowly falls by
about six displayed points per awake hour, hygiene by 1.5. Sleep reduces those
rates to 1.5 and 0.75 points per hour. A completed meal costs one hygiene point;
completed play costs two. Rejected actions have no cost. Health recovery retains
its need floor, and sleep alone does not substitute for feeding or cleaning.

```sh
./scripts/jelli-debug --socket build/pet.sock clock
./scripts/jelli-debug --socket build/pet.sock clock sync
./scripts/jelli-debug --socket build/pet.sock habits
./scripts/jelli-debug --socket build/pet.sock sleep-log
# For the board, replace --socket with --port and the discovered serial path.
```

The debug console shows these summaries and has a Sync clock button. Desktop sync
sets a session clock override, not the operating system clock. ESP32 sync writes
UTC to the PCF85063 RTC and records an explicit trust marker. A failed or unknown
RTC cannot silently invent offline time. TZ/hour/minute settings survive saves.

Save codec v3 adds habits, the sleep journal, clock settings, and nine collectible
prize types; v1/v2 saves migrate with unknown history. Newer unsupported files and
unreadable ESP32 checkpoint blobs are preserved. ESP32 disables writes to a
protected checkpoint and continues an unsaved session. Checkpoints use a bounded
4 KiB codec buffer; NVS supplies the device transaction, desktop uses alternating
files. Portable individual pet memory import/export remains planned.


## Pet collection and putting presents away

Settings → Pets opens a fixed 3×3 grid. Tap a slot for its unlock hint or owned
pet details. Bring out activates that pet; browsing and viewing Evolutions do
not switch pets. Back returns to the previous collection view. Linked sleep and
unfinished activities block activation, with a reason shown in the detail view.

Mint and Lilac occupy one Jelli family slot. A new game starts with Mint;
growth unlocks Lilac. In Evolutions, tap either unlocked form to select it,
even while sleeping or busy. The selected form persists without losing care
state or immediately evolving back. Locked forms cannot be selected.

The testing catalog adds Friend, Bubble, Garden, Pearl, Sunny, Tea, Movie, and
Moon families, unlocked by discovering their named present. Friend uses the
Friendship Bow unlock. They currently share the same Mint → Lilac artwork and 60-second prototype
growth rule. Evolution preserves identity and the collection slot. Stored pets
remain frozen. NEW marks a newly acquired companion until its details are viewed.

Tap a held present on the main scene, or its selected cell in Presents, to open
Give / Put away. Put away only deselects it; Back retains the selection. Give
checks the active recipient again and remains disabled for its original giver,
a sleeping pet, or a busy pet. Put away works in each of those cases.

The authored catalog is [content/pets.json](../content/pets.json). CMake validates
and compiles it for both hosts without a new runtime loader. Entry IDs 1–9 are
stable slot bindings; do not renumber them. This slice supports the existing
two-form evolution set; unsupported form/set references fail the build.

Save codec v8 reads v1–v7. The two legacy starter records merge into one Jelli
slot: the active starter keeps its identity and full care state (otherwise the
first starter is retained), and their reached forms are combined. Present
origins and the linked sleep journal follow the retained identity. Other family
records keep their state. A legacy Lilac also unlocks its Mint ancestor. The
inactive duplicate starter's separate care history is not retained.
Existing present discoveries can grant missing companions during migration.
A full nine-pet checkpoint uses 3569 of the available 4096 bytes. Older firmware
cannot read v8; keep a backup before downgrading.


## Food, water, and exercise

Care → Feed opens a grid of meals, fruit, and soup. Each costs one shared food
item when eating finishes. Meals add 30 fullness points; fruit adds 15 fullness
and 10 hydration; soup adds 22 fullness and 25 hydration. Care → Water fills
hydration to 100 without spending food. Swipe the home stat tile to Hydration.
Hydration slowly falls while awake and at one-quarter that rate while asleep.

Moments → Exercise starts a ten-second barbell workout. Starting costs 15
fullness, 20 hydration, and 5 energy points; completion adds 20 play points.
The action is disabled while asleep, busy, or short of those resources. It uses
fullness already eaten, so it does not spend another food inventory item. Costs
are paid once and remain paid after saving/resuming or interrupting with care.

Food effects live in [content/food.json](../content/food.json); workout duration
and effects live in [content/exercise.json](../content/exercise.json). CMake
checks bounds and compiles both catalogs without a runtime parser. Keep food IDs
stable because an unfinished meal stores its selected type. Older saves begin
at 70 hydration and resume any unfinished meal as the original meal type.


## Volume

Settings has large VOL − and VOL + controls at the bottom left and right. Each
tap changes the master level by 10 percentage points, clamped to 0–100. Zero
shows MUTED and suppresses automatic coos and menu sounds. Raising it previews
the new level with the normal menu tap. The setting persists across restarts and
applies to every pet. It remains available during sleep and activities.

The default is 65%: 30% above the previous gain settings, which correspond to
50% on this scale. At default, menu taps use codec level 33 and coos 23 (rounded
from the old 25 and 18). New and migrated saves use this louder default; v7 saves
preserve the chosen level, including mute. The debug CLI's explicit `sound
--volume` argument remains a raw 0–80 diagnostic override.


## Interaction feedback

Opening or confirming a menu plays a short two-note rising cue. Back and Close
use the original tap sound; accepted pet touches and care actions play a softer
voiced chirp. Swipe navigation follows the same direction distinction. Menu
navigation, settings, and collection selection no longer create sprite sprays.
Pet actions and celebrations retain them. Input audio remains limited to one
cue per 120 ms, and the master volume controls all automatic sounds.
