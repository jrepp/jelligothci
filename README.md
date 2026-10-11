# Jelligotchi

A small virtual pet for the **Waveshare ESP32-S3-Touch-AMOLED-1.75 (SKU 31261)**,
with a native SDL desktop version. Both run the same C11 game and renderer.

Care for your pet through meals, fruit, soup, water, play, and healthy routines.
Barbell workouts spend fullness, hydration, and energy.
Collect nine keepsakes and gift them to another pet. Sleep can track your own
bedtime and wake-up alongside the pet, with a rolling sleep score and a short
journal. This is a playable prototype; balance and artwork are still evolving.

## Download

Get the latest macOS or Windows build from
[GitHub Releases](https://github.com/jrepp/jelligothci/releases).
Native ZIPs include the game, authored PNGs, asset manifest, standalone preview,
and live-art tooling. SDL is bundled. Separate source-art and source-code archives
are also published. See [release details](docs/releases.md) for platform limits.

## Run locally

You need a C compiler, CMake 3.21+, Ninja, Make, and SDL2 2.0.18+.
On macOS, install Xcode command-line tools, then:

```sh
brew install cmake ninja sdl2
make run           # build, play, and save to build/pet-save.0 / .1
make run-live      # same game, with authored PNG hot reload
make jelli-art    # browser pixel editor with before/after review
```

First builds download the pinned local Python/asset tools. Nothing changes your
shell profile. [CONTRIBUTING.md](CONTRIBUTING.md) covers other platforms and setup.

Tap MENU or swipe up to open the ring. Swipe down goes back or closes it;
swipe sideways on the main scene changes the visible stat. Settings holds the nine-slot Pets
collection, sleep/wake, and the clock. Space pauses; Escape quits.

The executable starts an unsaved session unless given `--save BASE`. `make run`
enables saves. See [playing and persistence](docs/playing.md) for sleep, habits,
collections, and offline progress.

## Debug CLI

`make run` opens a local debug socket. In another terminal:

```sh
./scripts/jelli-viewer --socket build/jelli-debug.sock
./scripts/jelli-debug --socket build/jelli-debug.sock state
./scripts/jelli-debug --socket build/jelli-debug.sock habits
```

In Pets, tap a companion to inspect its evolution set or Bring out an owned pet.
Locked companions show their unlock hint. Tap a held present for Give or Put away;
Put away clears the selection and keeps the item.

The viewer is a readable event stream with game controls, cheats, clock sync,
and sleep history. The same protocol works over the ESP32 USB port. Details:
[debug commands and tunables](docs/playing.md#debug-cli).

On ESP32, `display benchmark-copy` compares full-frame row and bulk copies in
PSRAM; `display benchmark-rect` compares the original and current rectangle fills.
See the [benchmark command and timing limits](docs/playing.md#debug-cli)
before running this diagnostic, which briefly pauses rendering.

## Art workflow

Paint in the browser with `make jelli-art`, or edit PNGs under `assets/slice/`
in any editor, while `make run-live` runs. Valid edits refresh
without restarting the pet; bad or incomplete files retain the last valid art.
The **Activities** view authors shared recipes and previews their props on each pet.
Breakfast opens 6–11am, lunch 11am–2pm, tea 1–4pm, and dinner 4–9pm.
Finishing dinner unlocks dessert for that pet until midnight. One random fun
activity is offered each hour; use MORE in the game’s Activities ring to browse.
Yoga replaces Stretch and shares the same authored recipe from Health.
Style rules are in the [pixel art guide](docs/pixel-art-guide.md).
Bounds, centroids, and creature ground anchors update with the image.

```sh
./scripts/uv run --python 3.12 tools/assets/build_slice.py
open build/assets/preview.html    # macOS; otherwise open in your browser
```

The preview includes all 107 assets, including nine distinct presents.
[Art inventory](assets/slice/README.md) · [live authoring guide](docs/artwork.md)

### Creatures are content data

Species, animation and behaviour come from data and are generated into C at
build time ([ADR-012](docs-cms/adr/adr-012-data-driven-creature-species-and-presentation.md)):

| File | Defines |
| --- | --- |
| `content/pets.json` | Collection entries, forms (`art` name, portrait), evolution sets and growth |
| `assets/slice/assets.json` `clips` | Frames, holds and looping for each form's eight runtime poses |
| `content/creatures.json` | Per-form actor/icon/portrait scale, pose rules, idle beats, and how each behaviour state looks |
| `content/behaviors.json` | Behaviour states (curious, studying, asking for help…), their effects and requests, and each species' stimulus reactions |
| `content/activities.json` | Activity recipes: pet eligibility, timed unlocks, prerequisites, random weights, duration, gains/costs, pet cost/reward modifiers, jitter, locations, shared icons and animations |
| `content/locations.json` | Stable supported locations shared by activities and behavior; builds reject broken references |
| `content/potty.json` | The potty cycle from meals and drinks, accidents, and the mess animation |

Mint grows into Lilac; catching the Bubble Gem adds BUBBLE, who hatches as
BABY AXO and grows into the axolotl the same way. A happy touch makes the axolotl
hug. BUBBLE reacts to presents, the garden, night, reading and her needs:
she studies, contemplates, and asks for food, company or the potty (POTTY in the
Health ring). Ignore the potty request and she has an accident that CLEAN fixes
([RFC-005](docs-cms/rfcs/rfc-005-stimulus-driven-creature-behaviour.md)). An outstanding
urge waits through activities, sleep and cooldowns; offline catch-up never starts
a request or creates an unseen accident. Care can interrupt a moment and still
save and resume normally. Import upscaled frame art with `tools/assets/import_creature.py SPEC DIR`
(see `assets/slice/source/axolotl-import.json`), then edit clips in Jelli Art's
Creature view. Jelli Art's **Test in game** view renders a chosen scenario with
the real engine (`tools/game-preview/preview.c`, built with
`-DJELLI_BUILD_SDL=OFF -DJELLI_BUILD_PET=ON`) from the art and content on disk.

Activities pay their authored costs at start and reward completion. Rewards
vary within a bounded range; bond grows in small increments. Mint spends more
energy and grows into chess, science and fishing; Axolotl spends more hydration.
Jelli Art previews each pet's gains, costs and allowed locations. Home, Garden,
Park, Pond, Beach and Library have distinct backgrounds. TRAVEL cycles through
them; activities choose from their authored allowed places. See the
[activity balance audit](docs-cms/memos/memo-041-activity-balance-and-location-integrity.md)
for the full meter table, initial tuning and save migration behavior.

## Hardware bring-up

```sh
make esp-bootstrap
make esp-build
./scripts/esp ports
make esp-flash PORT=<discovered-port>
```

Use the repository-local SDK and discover the port each time. A successful build
or serial startup does not verify physical display, touch, or audio behavior.
[Hardware guide](docs/hardware.md) covers flashing, monitoring, current limits,
and the optional debug-port power experiments (`./scripts/esp validate`).
Normal firmware excludes the experiment runner; CI compiles normal, power, and standalone factory profiles.
See [factory validation](docs/factory-validation.md) for the non-destructive smoke image. All profiles share the `@J1` debug client;
use `jelli-debug capabilities` to discover commands and `factory status` for
factory state. The [ranked validation passes](docs/device-validation-passes.md) define
repeatable optimization experiments; factory inventory captures PMU, IMU and RTC baselines.
The RTC station pass checks progression without setting time or granting clock trust.
The IMU station pass qualifies command handshakes and awake motion interrupts before sleep trials.
The power profile also runs scripted touch recovery, input-timer comparisons,
and checked display sleep/brightness trials through the same debug path.
The ESP32 port also supports USB-configured Wi-Fi, automatic time/DST, and staged
HTTPS OTA updates; see [network setup](docs/hardware.md#wi-fi-time-sync-and-ota).
This network increment has been compiled but has not been flashed or verified on hardware.

## Development

```sh
make test          # shared core + desktop integration
make core-test     # no SDL dependency
make sanitize      # address / undefined-behavior checks
make hooks-check   # formatting, analysis, size limits, docs
```

The portable core uses injected time and caller-owned memory. SDL and ESP32 own
their run loops, input, and display buffers. See the
[architecture contracts](docs/development.md#boundary-between-engine-and-host)
before changing interfaces; follow [CONTRIBUTING.md](CONTRIBUTING.md).
[Device services](docs/development.md#device-services) inject clock, motion and
brightness providers; portable register drivers can be tested without hardware.
Desktop M injects a motion sample for development; it has no gameplay effect.

## Documentation

| Topic | Guide |
| --- | --- |
| Gameplay, saves, sleep, event console, CLI, tunables | [Playing and debugging](docs/playing.md) |
| PNG editing, hot reload, preview and asset validation | [Artwork](docs/artwork.md) and [source inventory](assets/slice/README.md) |
| Core/host contracts, checks, tool pins and CI | [Development](docs/development.md) |
| ESP32 setup, build, flash and device verification | [Hardware](docs/hardware.md) |
| Native ZIPs, release workflow and versioning | [Releases](docs/releases.md) |
| Contributor setup and validation | [CONTRIBUTING.md](CONTRIBUTING.md) |
| Repository access, recovery and maintainer handoff | [Maintainer guide](docs-cms/memos/memo-005-contributor-and-maintainer-handoff.md) |
| Latest slice changes and verification status | [Sleep, habits and collectibles](docs-cms/memos/memo-018-sleep-habits-and-collectible-presents.md) |
| Decisions, requirements and proposals | [Project memory index](docs-cms/README.md) |
| Planned game systems | [RFC-001](docs-cms/rfcs/rfc-001-virtual-pet-systems-architecture.md) and [pet memory proposal](docs-cms/rfcs/rfc-002-pet-memory-and-persistent-activities.md) |
