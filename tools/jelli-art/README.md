# Jelli Art

Browser pixel editor and before/after review for the slice artwork. Artist
instructions: [Paint in Jelli Art](../../docs/artwork.md#paint-in-jelli-art).
Style rules: [pixel art guide](../../docs/pixel-art-guide.md).

## Run locally

```sh
make jelli-art                      # http://127.0.0.1:8765/, edits this checkout
./scripts/uv run --python 3.12 tools/jelli-art/jelli_art.py --help
```

Local runs only write PNGs, `assets.json` bounds, `clips` and creature frame
assets, `source/hand-painted.json`, `source/studio-frames.json`, a creature
import spec's frame list, `content/creatures.json` and `content/behaviors.json`.
Commit the changes yourself. Behaviour saves need `cmake` on the path. For trials, `--assets` and `--content` serve copies of `assets/slice`
and `content/`. A run with `--assets` but no `--content` cannot edit creature data.

## Page shell

`shell.js` loads before the other page scripts, both in Jelli Art and in the
static page that `tools/assets/compare_slice.py` writes. It provides:

- Landmarks (header, mode `nav`, the asset `aside`, `main`) and a skip link.
- Mode tabs (Review, Paint, Creature, Behaviour and any other `#views`
  button). Arrow keys, Home and End move between tabs.
- Deep links: `#paint`, `#creature`, `#behaviour`, `#review`, or
  `#view=sheet&key=icons.feed&mode=diff&zoom=8`.
- One polite live region and toasts for save results, errors, stale files,
  git push problems and a lost server connection. Switching mode closes the
  info and done toasts of the mode left behind; warnings, problems and save
  results (`notify(text, {keep: true})`) stay.
- An unsaved changes button that lists edits and opens their mode. The page
  warns before closing with unsaved edits.
- System, dark, light and high-contrast themes. The page follows
  `prefers-color-scheme`, `prefers-contrast` and `prefers-reduced-motion`.
- A **?** overlay listing shortcuts, and a dismissible **Getting started**
  guide. The **Guide** button shows it again.

Panels use `window.JelliShell`, which exists when their script loads:

| Call | Effect |
| --- | --- |
| `registerShortcuts(section, [{keys: ['Mod+S'], description}])` | Lists shortcuts in the ? overlay. Registering a section again replaces it. `keys` lists alternatives, `+` joins a chord and `Mod` is ⌘ or Ctrl. Key handling stays in the panel. |
| `notify(text, {tone, sticky, hint, id, keep})` | Shows a toast and announces it. Tones are `info`, `ok`, `warn` and `bad`; `bad` stays until dismissed. `keep` keeps an `info` or `ok` toast across a mode switch. Reusing an `id` replaces that toast. `Studio.status()` calls this. |
| `announce(text)` | Speaks text through the live region without a toast. |
| `dialog({title, body, actions: [{label, value, primary, danger}], onOpen})` | Opens a modal `<dialog>`. Resolves with the chosen `value`, or `null` for Escape. Focus returns to the opener. |
| `confirm(message, {title, confirmLabel, danger})` | Resolves `true` or `false`. |
| `registerDirty(id, {label, mode, check})` | Adds unsaved state: `check()` returns a count or boolean. |
| `registerMode({id, label, view})` | Adds a tab that shows `state.view === view`. A button added to `#views` gets a tab automatically. |
| `activateMode(id)`, `openHelp()`, `setTheme('auto'\|'dark'\|'light'\|'contrast')`, `showGuide(bool)` | Switch mode, open the overlay, change the theme, show or hide the guide. |

Space or Enter on a button reached with the keyboard activates it. After a
click, holding Space still peeks at the before image. An element with
`role="button"` that has no `onkeydown` handler also gets Enter and Space. For
browser automation, `localStorage['jelli-shell:guide'] = 'false'` hides the guide.

## Paint tools

`studio.js` is the Paint mode UI; `paint_tools.js` holds its pixel logic as pure
functions (`window.JelliPaint`): Bresenham lines, rectangles, ellipses inscribed
in a pixel box, pixel-perfect strokes, flips, quarter turns, wrapped or clipped
shifts, colour replace, bounded flood fill, and a 100-step history with a
cursor. Tools paint only the asset's resolved palette (shared, a named palette
or an inline list) until the artist picks **custom**; paste refuses pixels
outside the target palette on the same terms. A selection is a rect, plus
floating pixels while it is moved or transformed, so moving it over other art
and back is lossless until the selection is dropped. Shortcuts are registered
with the shell's `?` overlay.

```sh
node tools/jelli-art/test_paint_tools.js
```

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

### Animation timeline

The clip editor is a timeline (`animation.js`). Each frame card has a
thumbnail, its duration and labelled buttons: **Paint ✎**, **◀ Move**,
**Move ▶**, **Duplicate** and **Remove**.
Drag a card, or press Alt+←/→ on it, to reorder. The duration track below sizes
each frame by its time and shows the playhead. Drag a handle, or focus it and
use the arrow keys (Shift or Page Up/Down for 100 ms), to retime a frame.
**Speed** (0.25× to 2×) only changes the preview. Playback follows elapsed
milliseconds, as `core/creature.c` `jelli_clip_frame` does.

**Duplicate current frame** and **Add blank frame** create a new PNG and
`creatures` asset with the form's size, pivot and palette. A blank frame starts
with one ink pixel at the pivot, because build_slice needs an opaque pixel.
New IDs are appended and existing IDs never move. A form with an import spec
(`source/axolotl-import.json`) takes the next `first_id` + position slot, and
the spec gets a `"studio": true` entry that `import_creature.py` skips. Other
forms take the next free ID in their hundred block. By default the frame is
inserted after the current one. A clean clip is saved in the same commit; a
clip with unsaved edits gets it in the working copy. **Retire** in the frame
library removes an unused frame's PNG and asset, and marks it retired in its
import spec. The studio refuses frames that a saved or unsaved clip, a
`pets.json` portrait or `content/creatures.json` still uses.
`source/studio-frames.json` records every added and retired frame, so a retired
ID or name is never handed out again.

Painting a clip frame shows the **Flip-book** (`flipbook.js`) beside the canvas:
a preview that plays the clip from the working pixels, so unsaved strokes
animate, a thumbnail per frame, and onion skin of one to three frames each side
(amber before, blue after), either only where they differ or as whole
silhouettes (Shift+O). Frames are compared with frame 1 as `core/pet_actor.c`
places them, each by its own ground anchor (`cr.groundAnchor`): the flip-book
warns when a frame slides by half a source pixel or more at the form's scale,
or when its eye height above its bottom edge changes (`JelliPaint.eyeRow`
measures the catchlight row). **Eye & ground** draws the ground line, anchor
and expected eye row. **Fit content** (Z) zooms the canvas to the
opaque bounds. ← and → change frame unless
the keyboard cursor shows; `,` and `.` always do; Shift+Space plays (Space stays
the before-image peek). The link under it returns to the timeline. Previews
start paused, so reduced motion needs no special case.

| Endpoint | Purpose |
| --- | --- |
| `GET /api/clips` | Poses, clips, named palettes, forms, frame cap |
| `POST /api/clips` | `{"clips": [{"key", "frames", "durations_ms", "loop"}], "bases"?: {key: clip_shas[key]}, "artist"}` |
| `GET /api/frames` | Per form: next frame ID, import spec, each frame's users, retired frames |
| `POST /api/frames` | `{"action": "add", "form", "from"?, "pose"?, "clip"?, "index"?, "duration_ms"?, "artist"}` or `{"action": "retire", "key", "artist"}` |
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
./scripts/uv run --python 3.12 tools/jelli-art/test_animation.py
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

## Saving safely

All saves run one at a time, including across two studio processes serving
the same files (`storage.WriteLock`). Each file is written to a temporary file
in the same directory, synced, then renamed into place. A save that changes
several files writes every temporary file before renaming any of them. Writes
stay inside `assets/slice` or `content/`, and the studio refuses to write
through a symlink.

Every save checks that the file has not changed since the page loaded it:

| Save | Base it sends | Where the page gets it |
| --- | --- | --- |
| `POST /api/save` | `"base"`: the PNG hash | `assets[].sha` in `/api/data`; the reply returns the new `sha` |
| `POST /api/clips` | `"bases"`: `{clip key: hash}` (`null` for a new clip) | `clip_shas` in `/api/data` |
| `POST /api/palette` | `"base"`: the slot's current colour | `palette` |
| `POST /api/creatures`, `/api/content` | `"base"` / `"bases"` (required) | `creature_data_sha`, `behavior_data_sha` |

If the base does not match, the reply is `409` with
`{"error", "stale": true, "current"}`. The page can ask the artist and save
again with `current` as the base. Saves that leave out the base are still
accepted, so older pages keep working. Other errors use these statuses:

- `400`: a missing or wrongly typed field, invalid JSON, or a validator
  rejecting the edit.
- `411`, `413`, `415`: no `Content-Length`, a body over 1 MiB, or a body that
  is not `application/json`. Plain cross-site form posts get `415`.
- `405`: the wrong method.
- `503` (`"retry": true`): another save held the lock for 60 s.
- `500`: an internal error. The server log has the traceback, and the server
  keeps running.

On start, the studio deletes temporary files left by an interrupted save and
reports JSON files it cannot parse in `GET /healthz` under `problems`.
`/healthz` also reports `capabilities` (`cmake`, `git`, `node`, `tidy`). If
`cmake` is missing, the Behaviour view is read-only and
`behavior_readonly_reason` gives the reason.

With `--git-branch`, each save is committed with `git commit -- <its files>`,
so changes someone else staged stay out of the commit. If a commit fails (for
example, a held `index.lock`), the save stays on disk and the commit is
queued. The queue is retried before the next commit and in the background.
`GET /api/git` shows `pending_commits`, `last_commit_error`, `last_push`
(`ok`, `kind`: `network`, `auth`, `rejected` or `error`) and `next_retry_at`.
`POST /api/git/retry` retries now. The page shows queued commits in the
header and as a `JelliShell` toast.

Failed pushes back off from 15 s to 10 minutes. A rejected push is repaired
when that is safe:

- If the remote branch's studio files already match the base branch (merged
  or squashed), the remote branch is replaced with `--force-with-lease`.
- If the remote has new commits, the studio rebases onto them, and aborts if
  they conflict.

On start, studio files a crash left uncommitted are committed
(`recover studio edits left uncommitted`).

## Drafts

`drafts.js` (`window.JelliDrafts`) keeps unsaved edits in this browser's
`localStorage`, one per `(kind, key)`, together with the base hash the edit
started from. Paint uses it: each stroke is stored after a short pause and on
`pagehide`, saving or reverting drops the draft, and reloading restores it with
a notice. If the file changed since the draft was made, restoring still works.
Saving the restored draft then gets a `409`, and the page asks before
overwriting. Other editors can call `put`, `restore`, `drop`, `list`, `later`
and `flush`; the header comment of `drafts.js` documents each call and
suggests keys.

## Tests

```sh
./scripts/uv run --python 3.12 tools/jelli-art/test_server.py   # HTTP end to end, no browser
tools/jelli-art/browser_smoke.sh [port]                        # optional, needs agent-browser
```

The **Studio tests** job in `.github/workflows/jelli-art.yml` runs
`test_paint_tools.js`, `test_server.py`, `test_creatures.py`,
`test_animation.py`, `test_game_preview.py` and `test_tidy.py` (Tidy gives the
same pixels in every process) on every pull request, and a
release image is built only after they pass.

`test_server.py` starts real servers on scratch copies and a scratch git
checkout with a bare origin. It covers:

- paint, clip, behaviour, creature and palette saves, with stale refusals
- malformed requests and concurrent saves
- orphan cleanup and corrupt JSON at start
- a host without `cmake`
- commits that leave unrelated staged files alone, queued commit retries and
  crash recovery
- pushes, push failure and retry, push repair by rebase and by replacing landed
  work
- `entrypoint.sh` keeping unlanded `content/` commits and starting offline
- the drafts API (with `node`)

`browser_smoke.sh` drives the real page. It paints a stroke, checks the draft
survives a reload, saves, and checks that saving over a file changed on disk
asks first (Cancel keeps the newer file; OK overwrites it).

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
the probe. On start, the studio branch resets to the base branch once its
work has landed there, whether by merge or squash. The check covers both
`assets/slice` and `content/`. Unmerged work and uncommitted or new studio
files are kept. A stale `.git/index.lock` or an interrupted rebase is cleared
first. If the fetch fails (offline), the studio starts from the local checkout
and retries pushes later. `JELLI_HOST` overrides the bind address (default
`0.0.0.0`), and `JELLI_APP` the code directory.

## Versioning and releases

Jelli Art has its own version (`VERSION`, tags `jelli-art-vX.Y.Z`); see ADR-011.
Release Please opens the release PR. When it merges,
`.github/workflows/jelli-art.yml` publishes `ghcr.io/jrepp/jelli-art` as `X.Y.Z`,
`X.Y`, `X`, and `sha-<commit>`.

## Hosting

`jelli-art.home.jrepp.com` on nuc, over Tailscale, is defined in t1-hosting
(memo-039, `jelli-art/` and `scripts/jelli-art-deploy`).
