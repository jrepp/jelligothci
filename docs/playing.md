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
Close `esp-monitor` and other serial clients before using the CLI. Firmware startup after flashing is verified; this feature has host test coverage,
but live debug commands and screenshot transfer have not yet been exercised.

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
There are four pets: two jellies, each shown as MINT until it grows into LILAC,
Nirmal the ninja (shown as NIRMAL), and the unicorn (shown as UNICORN). The ninja
and the unicorn keep their forms. A new player starts with the unicorn. Settings >
SWITCH PET moves to the next pet in order (first jelly, second jelly, Nirmal, the
unicorn, then back to the first) and shows NOT READY while the current pet is busy or resting under the
sleep journal. Stored pets pause until selected. Saves from before the ninja gain
a fresh ninja, and saves from before the unicorn a fresh unicorn, the first time
they load; they keep the pet that was active when they were saved.
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
→ creature override. All four forms (Mint, Lilac, the ninja, and the unicorn) currently inherit
common defaults. This establishes a content extension point without inventing
different balance for the creatures.

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

Six tiny procedural cues are available through `jelli-debug sound`: `chirp`,
`happy`, `sparkle`, `hello`, `sleepy`, and `tap`. Hello and sleepy are vowel-like pet voices.
Accepted menu presses play a quiet 65 ms tap, coalesced within 120 ms, at the
Settings [sound level](#sound-level) (volume 25 at the default level).
Pass `--volume 0..80` (default 35); this command always plays at exactly that
volume, ignoring the Settings level. Both native SDL and the ESP32 support this
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
steady-state synthesis uses fixed buffers and a four-request queue. Request volumes
are amplitude percentages on both hosts. The ESP32 installs a codec volume curve
of 20*log10(v/100) + 14 dB, which uses the synth's headroom (peaks near -17 dBFS) so
that game cues are audible; the codec's default curve played them at about -40 dB.

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

## Battery indicator

The resting home scene shows a small battery icon and caption in the upper left.
Bars show the level: none with an exclamation mark below 10 percent, one bar
from 10, two from 25, and three from 70. Below 25 percent the caption reads
`LOW 8%` in pink, so low charge never relies on color alone. Charging shows a
bolt with the percentage, a completed charge shows `FULL`, external power with no
battery shows a plug and `PLUGGED`, and an unknown level shows `--%`. The
indicator is hidden while a ring, the collection, or an activity is open.

Settings has a BATTERY toggle (bottom left) with an ON/OFF badge; it is on by
default and saved with the game. When it is off the indicator disappears and
battery readings cause no redraws. The desktop reads the operating system's power
state every five seconds (headless runs do not, so snapshots stay deterministic).
The ESP32 reads the board's AXP2101 PMU every ten seconds without changing any
PMU setting; see [the hardware guide](hardware.md#battery-status).

To preview the art without a battery, override the reading for this session. The
override is never saved; `clear` returns to the host reading.

```sh
./scripts/jelli-debug battery                    # current reading, override, caption
./scripts/jelli-debug battery discharging 8      # LOW 8%
./scripts/jelli-debug battery charging 42
./scripts/jelli-debug battery charging unknown   # percent unknown
./scripts/jelli-debug battery no-battery         # also: unknown, charged
./scripts/jelli-debug battery clear
```

States are `unknown`, `no-battery`, `discharging`, `charging`, and `charged`;
the percent is 0–100 or `unknown` (the default). `state` includes the same
`visual.battery` object. Add `--port` for the ESP32.



## Sound level

Settings has a SOUND button on the left of the ring, drawn with the music-note
art. Its badge shows the current level, 0 to 10, or OFF when muted. The default
is 5, the middle of the range. Tap SOUND to open the level editor: the center
shows the level as a large number above the word SOUND and a ten-segment bar.
Filled segments are solid and empty ones are hollow outlines, so the level does
not depend on color. The caption below reads `LEVEL 5 OF 10`, or `MUTED` at 0.
VOL − (left) and VOL + (right) change the level by one step. VOL − at 0 and
VOL + at 10 report NOT READY and are dimmed. Back, or a downward swipe, returns
to Settings, as in the clock editor. Each change is saved with the game, and the
confirming tap plays at the new level, so you hear the change. At 0 the game is
silent: hosts request no taps or coos.

The level scales each cue's host volume linearly, rounded to the nearest step,
so level 5 reproduces the original fixed volumes and level 10 doubles them:

| Level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Tap | — | 5 | 10 | 15 | 20 | 25 | 30 | 35 | 40 | 45 | 50 |
| Other cues | — | 4 | 7 | 11 | 14 | 18 | 22 | 25 | 29 | 32 | 36 |

Volumes use the hosts' 0–80 request scale: SDL scales samples linearly and the
ESP32 sets the ES8311 codec's output volume. How loud each level sounds on the
device speaker has not been measured. `jelli-debug state` reports the setting as
`visual.sound` (`level`, `max`, `edit`, `tap_volume`, `voice_volume`). Opening
the editor logs input code 42; VOL − and VOL + log 43 and 44.

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
prize types; v1/v2 saves migrate with unknown history. Codec v4 adds the battery
display setting; earlier saves load with it on. Codec v5 keeps the v4 layout and
gives older two-pet saves the ninja. Codec v6 adds the sound level; earlier saves
load level 5, and values above 10 are rejected as corrupt. Codec v7 keeps the v6
layout and gives older saves the unicorn; a v7 roster needs at least four pets.
Newer unsupported files and
unreadable ESP32 checkpoint blobs are preserved. ESP32 disables writes to a
protected checkpoint and continues an unsaved session. Checkpoints use a bounded
4 KiB codec buffer; NVS supplies the device transaction, desktop uses alternating
files. Portable individual pet memory import/export remains planned.
