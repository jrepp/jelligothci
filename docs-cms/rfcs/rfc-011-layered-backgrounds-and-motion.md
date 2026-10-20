---
title: Layered backgrounds, tilt parallax and sideways travel
status: Draft
author: Codex
created: 2026-10-20
tags: [assets, jelli-art, motion, rendering, scenes]
id: rfc-011
project_id: jelligotchi
doc_uuid: 053bc1df-a5ad-43d3-8507-68fcdd14af57
---

# Summary

Author scenes as ordered references to reusable background artwork. Drive apparent
depth with two independent presentation inputs: normalized device tilt and travel
distance. The owner requested tilt scenes and city walking, with sideways travel
first and forward travel eventually. This RFC proposes the portable engine design;
it does not approve a production sensor policy or gameplay travel defaults.

The first increment implements the [studio editor](../../tools/jelli-art/scenes.js),
[validated authoring schema](../../tools/assets/scene_data.py),
[starter scenes](../../content/scenes.json), and the
[reproducible art kit](../../assets/slice/source/background_kit.py).
The continuation implements [portable composition](../../include/jelli/scene.h),
explicit scene selection, SDL synthetic controls and saved-scene Test in game.
[Runtime evidence](../memos/memo-080-background-scene-runtime.md) records the
implemented boundary. Normal gameplay still uses flat location backgrounds.

# Existing boundaries

[The renderer](../../core/pet_atmosphere.c) samples a selected scene cache or the
flat location image, then applies night shading and a circular mask. [Location definitions](../../content/locations.json)
keep stable identities used by activities and saves. Preserve those identities and
the background asset fallback during adoption.

[The device contract](../../include/jelli/device.h) exposes a consumed motion-event
sample, not orientation. Its detected flag cannot drive tilt. Follow the
[injected device boundary](../adr/adr-014-injected-device-services.md): acquire
sensor data outside engine/render callbacks, and publish already acquired samples.
Do not reinterpret motion events as pickup, tilt, or walking distance.

[The bounded-memory proposal](rfc-009-bounded-memory-systems-and-adoption.md) and
[Jellipak residency proposal](rfc-008-jellipak-resource-store.md) own memory and
loading policy. Layer composition must work with current immutable embedded assets
first; this feature does not require adopting the entire resource system.

# Authored scene contract

The versioned JSON document is authoritative authoring data; it is not a binary
runtime format. The [validator](../../tools/assets/scene_data.py) owns current
capacities, ranges, strict field checking and reference validation. Scene keys are
stable authoring identities, independent of location IDs. A scene has a name, an
opaque base fill and an ordered list of layers, back to front. Each layer references
registered background artwork and carries source-pixel x/y placement, horizontal
and vertical tilt amplitudes, a travel percentage, repeat-x and visibility flags.

Layer names and optional initial variation are now authored alongside these fields.
A scene seed and stable per-layer seed select bounded position offsets, travel
percentage variation and layer appearance. The shared compiler resolves these into
ordinary immutable definitions; the browser uses the same integer sampling rule.
The saved seed reproduces the arrangement without per-frame randomness. Older
scene files default to unvaried layers and asset-based labels. Per-repeat variation,
independent traffic motion and randomized spawn scheduling remain later work.

The current viewport is 64 source pixels square. All layers remain behind the pet;
foreground occlusion, scale, arbitrary rotations, animated layer clips and opacity
blending are separate future increments. Images use the existing binary alpha and
RGB565 asset pipeline. A layer instance never copies or mutates its source pixels.
Changing shared art changes all scenes that reference it. Duplicate a scene to
rearrange its own layer instances while keeping their artwork shared.

The muted kit uses foliage, clay and atmospheric ramps. The palette lives beside
its [source geometry](../../assets/slice/source/background_kit.py) and is registered
in the [asset manifest](../../assets/slice/assets.json). Existing pet palettes stay
independent. New background artwork defaults to the kit palette; artists may create
a layer without registering a gameplay location. The authored examples demonstrate
reuse across a street, a garden walk, and a town garden. Their motion values are
preview starting points, not approved gameplay defaults.

# Motion model

Let T be normalized tilt in [-1, 1] per axis, D signed travel distance in source
pixels, A the authored tilt amplitude and P the authored travel percentage.
The browser prototype uses these source-space offsets:

```text
x = round(base_x - T.x * A.x - D * P / 100)
y = round(base_y - T.y * A.y)
```

Positive travel moves scenery left. The pet stays independently placed and animated.
Near layers can have larger amplitudes/speeds than distant layers. Zero tilt
amplitude and zero travel percentage pin a layer. The systems can operate separately
or together. Playing travel in the editor is explicit; sliders work without sensor
permissions and Reset motion restores neutral tilt and zero travel.

Repeat horizontally using Euclidean modulo by image width, including for negative
travel. Draw every intersecting repeat, clipped to the viewport. Traveling layers
must repeat. A non-repeating layer clips normally and reveals the base fill where
uncovered. Sky/base art should cover the viewport; vertical tilt needs overscan or
a compatible base fill. Do not bake a circular vignette into reusable moving pieces;
clip the composite at the display boundary. Existing vignetted flat backgrounds
remain available as single-layer compositions with motion initially disabled.

The [runtime](../../core/scene.c) represents normalized tilt and fractional travel
in bounded fixed-point values, with range-checked intermediate arithmetic. Specify rounding
as floor(value + 0.5), including negatives, to match the browser prototype. Preserve
a fractional phase per layer and reduce it by that layer's repeat period rather
than accumulating an unbounded world coordinate. A bounded signed travel delta is
independent of frame rate; tests inject time/distance directly. Saturate invalid or
excessive inputs before arithmetic. No floating-point math is required per pixel.

# Proposed portable engine interfaces

The table includes proposed sensor and controller responsibilities. Implemented
scene interfaces and storage contracts live in [scene.h](../../include/jelli/scene.h)
and [pet_scene.h](../../include/jelli/pet_scene.h).

| Boundary | Proposed data and ownership |
| --- | --- |
| Host orientation acquisition | Timestamped normalized tilt, valid flag and display-axis convention; immutable latest sample, nonblocking poll |
| Portable tilt presentation | Neutral calibration, dead zone, elapsed-time smoothing, clamps, explicit stale/unavailable behavior |
| Travel controller | Signed bounded distance delta from an activity/controller; never inferred from an IMU event |
| Scene definition | Immutable validated layer table and deduplicated asset dependencies |
| Scene instance | Caller-owned per-layer phases and current quantized offsets |
| Renderer | Read stable resident asset views, clip/sample masks and composite back to front |

Keep the existing motion-event interface for its existing consumers. Add an optional
orientation provider rather than extending the meaning of its detected flag. Define
x positive toward the display's right and y positive down after host rotation.
Neutral calibration belongs to presentation setup, not saved pet progression. On
startup, absent hardware or a stale/invalid sample, target neutral smoothly rather
than holding a stale extreme tilt. Expose a reduced-motion setting that suppresses
tilt and decorative automatic movement. Exact freshness, filtering and dead-zone
values need device evidence and owner review.

The portable update computes offsets once per frame, using injected monotonic time.
Publish one complete scene render key containing quantized offsets and an asset
revision. Unchanged quantized output produces no new damage. Any changed background
layer initially invalidates the full scene region so old pixels are restored before
the pet and UI are redrawn. Never let an actor-only restore path sample the old flat
background after layered adoption. A later measured optimization can narrow damage;
correct full-region composition comes first.

Render into the host-owned surface using existing clipping, stride and RGB565
contracts. No heap allocation, file reads, sensor IO or decompression in update/draw.
Bound layer traversal and visible repeat copies using validated dimensions and
capacities. Composite coverage masks, then apply shared night shading/circular
clipping consistently. Retain integer nearest sampling to preserve pixel edges.

# Resources and adoption

The authoring compiler resolves keys to stable asset IDs, rejects missing or
wrong-kind references, emits immutable tables and reports unique image bytes per
scene. Repeated references share one resident view. With Jellipak later, pin the
complete deduplicated dependency set before scene publication; retire the previous
set only after its last render. If validation/loading fails, retain the previous
complete scene or its explicit flat fallback. Do not publish a partially loaded kit.

1. Authoring and palette increment: implemented here, with no location/schema/save
   migration. The scene JSON now also feeds generated immutable runtime tables.
2. Portable composition: implemented table compilation, deterministic transforms and
   clipped sampling behind explicit scene selection, preserving flat fallbacks.
   SDL and Test in game provide synthetic tilt/travel; production sensors are pending.
3. Sideways travel integration: select a scene for a specific activity, connect its
   distance to walking animation, and establish transition/cancellation semantics.
   Travel is presentation; it does not change pet progression by itself.
4. Qualified device tilt: acquire orientation off the render path and verify neutral,
   display rotation, noise, latency, power and physical behavior separately.
5. Forward travel: add an explicit projection mode and perspective/depth contract.
   Do not emulate it by overloading sideways speed or allowing arbitrary layer scale.

# Acceptance gates and unresolved decisions

Studio tests must cover malformed/oversized documents, foreign references,
read-only roots, stale writes, immutable save snapshots and draft recovery. Browser
checks cover selecting/reordering layers, shared-art composition, reverse travel,
neutral reset and continued edits during pending saves. Asset validation enforces
palette membership, dimensions and binary alpha.

Runtime tests must compare synthetic offsets and rendered pixels for both tilt
extremes, negative/repeated travel, phase wrap, zero/large time deltas, missing and
stale sensors, scene transitions, dirty regions, strided surfaces and absent assets.
Run sanitizers and core-only tests with device capacities. Measure CPU time, surface
copy cost, unique resident bytes and worst-case layer coverage. A successful firmware
build is not proof of acceptable frame rate or physical tilt behavior.

Still to choose: the first gameplay location/activity using a scene, travel distance
semantics, gameplay scene transition policy, calibrated tilt/filter values, device
resource qualification and reduced-motion settings placement. Forward perspective, procedural chunk streets,
foreground occlusion and animated props remain outside this first increment.