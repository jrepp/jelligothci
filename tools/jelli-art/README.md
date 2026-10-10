# Jelli Art

Browser pixel editor and before/after review for the slice artwork. Artist
instructions: [Paint in Jelli Art](../../docs/artwork.md#paint-in-jelli-art).
Style rules: [pixel art guide](../../docs/pixel-art-guide.md).

## Run locally

```sh
make jelli-art                      # http://127.0.0.1:8765/, edits this checkout
./scripts/uv run --python 3.12 tools/jelli-art/jelli_art.py --help
```

Local runs only write PNGs, `assets.json` bounds and `clips`,
`source/hand-painted.json`, `content/creatures.json` and `content/behaviors.json`.
Commit the changes yourself. Behaviour saves need `cmake` on the path. For trials, `--assets` and `--content` serve copies of `assets/slice`
and `content/`. A run with `--assets` but no `--content` cannot edit creature data.

## Creature clips

The **Creature** view edits the per-pose clips in `assets.json` (`clips`, keyed
`<form>.<pose>`) for each creature form, named from `content/pets.json` forms
when they carry `art`. Clip IDs, keys and the pose list (`creature_poses`) are
fixed; frames, durations and the loop flag are editable. State poses
(`state_poses`, such as `study`) follow the 8 base poses. A form may lack a clip
for one, in which case the fallback base pose plays and the view says so. **Add
clip** starts one from the fallback; **Remove clip** deletes it, so the fallback
plays again. The preview picks the
frame from elapsed time and draws it as the game does: the form profile's actor
scale, ground anchor at (233, 256) on the round 466 px panel. Reduced motion
starts it paused.

Saves change only the edited `clips` entries. Before writing, the studio runs
the served checkout's `tools/assets/build_slice.py` `load_assets()` on a scratch
copy. If the unedited manifest already fails (for example, a live paintover with
a custom colour), the clip rules are still checked and the save reports that
failure as a warning. Sprites with their own palette (`"palette": "axolotl"` or
an inline list) paint from that palette; ✎ and **Tidy outline** apply to the
shared palette only.

| Endpoint | Purpose |
| --- | --- |
| `GET /api/clips` | Poses, clips, named palettes, forms, frame cap |
| `POST /api/clips` | `{"clips": [{"key", "frames", "durations_ms", "loop"}], "artist"}` |
| `GET /api/creatures` | `content/creatures.json`, its hash, validator limits, editability |
| `POST /api/creatures` | `{"data": <whole document>, "base": <hash>, "artist"}` |
| `GET /api/behaviour` | `content/behaviors.json`, its hash, vocabulary, `potty.json`, creature data |
| `POST /api/content` | `{"docs": {"behaviors"?, "creatures"?}, "bases": {name: hash}, "artist"}` |

A clip edit `{"key", "remove": true}` deletes a state pose clip. An edit for a
missing state pose clip creates it, with the next free ID after its form's clips.

## Behaviour and size

**Behaviour & size**, below the clip editor, edits `content/creatures.json` for
the selected form. The profile sets actor, icon and portrait scales; the page
shows each size against the panel and collection-cell limits. The behaviour
holds first-match-wins pose rules, a 900 ms idle beat schedule, and a quiet
cycle. **Simulate** applies the chosen conditions or idle to the panel preview.
The idle timeline plays the beats with their clips. Edits to a behaviour that
several forms share show a warning.

Saves replace the whole file, normalised to `json.dumps(indent=2)`. The page
sends the hash it loaded, so a save never overwrites unseen changes. The served
checkout's `tools/assets/creature_data.py` `load()` validates a scratch copy
first. Like `build_slice.py`, it comes from the checkout, not the image.

## Behaviour

The **Behaviour** view edits `content/behaviors.json` (RFC-005): night bounds,
the low-need threshold, states, and repertoires with their forms, affinities and
reactions. It also edits each state's look in `content/creatures.json`
(`state_presentation`): pose, caption, and effect and prop sprites. Renaming or
deleting a state updates its reactions and look. Stimulus, need, command,
health, care and location names come from the `set()` lists in the checkout's
`cmake/JelliBehaviors.cmake`. The simulator's numbers come from the enums in
`include/jelli/game.h` and `behavior.h`.

**Stimulus simulator** picks a form, a stimulus and its value, the pet's
conditions, the running and cooling-down states, a pet ID and a tick. It lists
every reaction with the reason it fits or not, the weighted pick, the chance roll
and the resulting state. It also shows the odds over the next 600 behaviour
seconds and previews the look: the clip at profile scale, the caption, effect,
prop and, optionally, the potty mess from `content/potty.json`. `simulator.js`
mirrors `roll()`, `reaction_fits()`, `react()` and `deliver()` in
`core/behavior.c`. It also mirrors the placement in `core/pet_behavior_draw.c`.
Night is an input there; the game derives it from the pet clock.

**Save behaviour** sends both files with the hashes the page loaded. The server
runs `cmake/JelliBehaviors.cmake` with `cmake -P` on a scratch copy holding the
candidate and the checkout's `activities.json` and `pets.json`. `creature_data.py`
`load()` and `load_looks()` then check the looks against the candidate states.
Both files are written in one commit. The container image installs `cmake`.

```sh
./scripts/uv run --python 3.12 tools/jelli-art/test_creatures.py
```

`testdata/behavior_engine_cases.json` holds 300 outcomes recorded from the real
`core/behavior.c`; the tests replay them through `simulator.js` when `node` is
installed.

## Test in game

**Test in game** renders a scenario with the real C engine instead of a JS
imitation. Pick a pet and form, a behaviour state, a moment, sleep, the potty
mess and urge, the five needs, a page (ring pages can show the menu open), the
clock, and the frame timing: start offset, frame count (up to 48), interval, and
optionally seconds of simulated play before the first frame or between frames.
**Render in engine** (or Enter in any field, Ctrl/⌘+Enter elsewhere) shows a
large frame, a strip of thumbnails (←/→ step frames; Play is off under reduced
motion) and what the engine accepted: form, pose, behaviour, activity, clock and
needs. Each frame's alt text describes the scenario. **Render after every save**
renders again after a save in any view, or when files change on disk.

The server mirrors the checkout's `CMakeLists.txt`, `cmake/`, `core/`,
`include/` and the generator scripts, plus the served art and content, into
`build/jelli-art-preview/src` (a temp directory when the checkout is read-only;
`JELLI_PREVIEW_CACHE` overrides it). It builds `tools/game-preview/preview.c`
there with `-DJELLI_BUILD_SDL=OFF -DJELLI_BUILD_PET=ON`, rebuilding only when a
file changed. Generators run with the studio's own Python, so a bad edit fails
here exactly as in a real build. The page shows the failing step inline:
content checks (`cmake/Jelli*.cmake`), art checks (`build_slice.py` through
`embed_slice.py`), or C compile errors, with the build log. Unsaved edits are not
included; save first.

Builds are bounded at 300 s with one build at a time, renders at 30 s with two
at a time. Without `cmake` or a C compiler the view says so and stays read-only.
The container image installs `cmake` but no compiler. Scenarios that the
engine refuses, such as a moment it rejects, report the engine's own reason.

| Endpoint | Purpose |
| --- | --- |
| `GET /api/game-preview` | Availability, pages (from `pet_ui.h`), pets and forms, states, moments, limits |
| `POST /api/game-preview` | `{"scenario": {"pet", "form", "page", "menu", "behavior", "moment", "asleep", "mess", "potty", "needs": {...}, "minute", "start_ms", "frames", "step_ms", "advance_s", "live"}}` |

A bad scenario returns 400 with `errors` per field. A build or generator
failure returns 200 with `ok: false`, `stage` and `messages`. A missing
toolchain or a full render queue returns 503.

```sh
./scripts/uv run --python 3.12 tools/jelli-art/test_game_preview.py
```

The real-render cases skip when cmake or a compiler is missing.

## Container

```sh
podman build -f tools/jelli-art/Containerfile -t jelli-art .     # or docker build
# Serve an existing checkout (read-only review):
podman run --rm -p 127.0.0.1:8765:8765 -v "$PWD:/work/jelligotchi:ro" jelli-art
```

| Variable | Meaning |
| --- | --- |
| `JELLI_REPO` | Checkout path inside `/work` (default `/work/jelligotchi`) |
| `JELLI_GIT_URL` | Clone URL when the checkout is missing (public HTTPS by default) |
| `JELLI_BASE_BRANCH` | Branch a new studio branch starts from (default `main`) |
| `JELLI_STUDIO_BRANCH` | Commit every save to this branch; unset means no git writes |
| `JELLI_GIT_PUSH` | `1` pushes the studio branch in the background |
| `JELLI_PUSH_URL` | SSH push URL, for example `git@github.com:jrepp/jelligothci.git` |
| `JELLI_DEPLOY_KEY` | Private key path (default `/run/secrets/jelli-art-deploy-key`) |
| `GIT_AUTHOR_NAME`, `GIT_AUTHOR_EMAIL` | Commit identity (default `Jelli Art`) |

The container listens on 8765 and answers `GET /healthz`; `healthcheck.py` is
the probe. On start, the studio branch resets to the base branch once its art
has landed there, whether by merge or squash. Unmerged art is kept.

## Versioning and releases

Jelli Art has its own version (`VERSION`, tags `jelli-art-vX.Y.Z`); see ADR-011.
Release Please opens the release PR. When it merges,
`.github/workflows/jelli-art.yml` publishes `ghcr.io/jrepp/jelli-art` as `X.Y.Z`,
`X.Y`, `X`, and `sha-<commit>`.

## Hosting

`jelli-art.home.jrepp.com` on nuc, over Tailscale, is defined in t1-hosting
(memo-039, `jelli-art/` and `scripts/jelli-art-deploy`).
