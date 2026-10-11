# Jelligotchi

A small virtual pet for the **Waveshare ESP32-S3-Touch-AMOLED-1.75 (SKU 31261)**
with a native SDL desktop version. Both run the same C11 game and renderer.
Care for, play with, and collect companions for your pet. This is a playable
prototype; gameplay balance and artwork continue to evolve.

## Download

Get the latest macOS or Windows build from
[GitHub Releases](https://github.com/jrepp/jelligothci/releases). Native ZIPs
include the game, authored art, preview, and live-art tooling. See
[release details](docs/releases.md) for package contents and platform limits.

## Run locally

You need a C compiler, CMake 3.21+, Ninja, Make, and SDL2 2.0.18+. On macOS:

```sh
brew install cmake ninja sdl2
make run          # build and play with desktop saves
make jelli-art    # open the browser pixel editor
make run-live     # optional: reload edited art while the game runs
```

First builds download the pinned local Python and asset tools. Setup for other
platforms is in [CONTRIBUTING.md](CONTRIBUTING.md). The executable starts an
unsaved session unless given `--save BASE`; `make run` enables saves.

Tap MENU or swipe up to open the ring; swipe down goes back or closes it. Swipe
sideways on the main scene to change the visible stat. Settings contains pet
selection, sleep/wake, and the clock. Space pauses; Escape quits. The
[playing guide](docs/playing.md) covers gameplay, saves, debug commands, and
offline progress.

## Art and content

Edit PNGs under [`assets/slice/`](assets/slice/) or use `make jelli-art`. For
activity recipes and creature content, edit the files under `content/`. See the
[artwork guide](docs/artwork.md), [source inventory](assets/slice/README.md),
and [pixel art guide](docs/pixel-art-guide.md) for the authoring workflow.

## ESP32

```sh
make esp-bootstrap
make esp-build
./scripts/esp ports
make esp-flash PORT=<discovered-port>
```

Use the repository-local SDK and discover the port each time. A successful
build or serial startup does not verify physical display, touch, or audio.
See the [hardware guide](docs/hardware.md) for setup and device verification.

## Development

```sh
make test
make core-test
make hooks-check
```

The [development guide](docs/development.md) describes architecture contracts
and checks. [CONTRIBUTING.md](CONTRIBUTING.md) covers contributor setup; see
[release details](docs/releases.md) for versioning and release workflow.
