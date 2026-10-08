---
title: Garmin port feasibility
author: Codex
created: 2026-10-08T03:00:09Z
tags: [garmin, memo, research]
id: memo-013
project_id: jelligotchi
doc_uuid: ccf2c1da-759c-4d37-aa9a-075875859897
---

# Finding

Jelligotchi is feasible as a Connect IQ device app on a compatible Garmin watch.
This is research, not an implemented port or an approved platform decision. The
target model is still unspecified; its SDK device definition must determine the
actual memory, display, input, and API limits.

# Platform Constraints

Garmin's public app platform uses Monkey C bytecode in a virtual machine. Our C11
core therefore needs a Monkey C implementation for the normal Connect IQ route;
the current ESP32 binary and C host adapter cannot simply be relinked. The stable
content IDs, PNG sources, formulas, and deterministic behavioral fixtures can be
reused as inputs/specification. [Monkey C reference](https://developer.garmin.com/connect-iq/reference-guides/monkey-c-reference/).

A device app is the appropriate first target for interactive care and ring menus.
Watch faces have tighter API and low-power restrictions. A passive pet watch face
could be a later, separately designed experience. Background temporal services
run no more frequently than every five minutes and for at most 30 seconds per
invocation; the pet must save state and compute bounded elapsed progress on resume.
[App types](https://developer.garmin.com/connect-iq/articles/connect-iq-basics/App_Types.html),
[background services](https://developer.garmin.com/connect-iq/connect-iq-faq/how-do-i-create-a-connect-iq-background-service/).

Garmin's graphics API provides drawing contexts, shapes, bitmaps, and clipping.
Use those facilities with precompiled PNG resources instead of copying our two
466x466 RGB565 buffers into the app heap. The current packed C particle layout
cannot be assumed to have identical memory cost in Monkey C's object model;
prototype compact arrays/byte storage and measure allocations before selecting
an effect count. [Graphics API](https://developer.garmin.com/connect-iq/api-docs/Toybox/Graphics.html),
[language memory model](https://developer.garmin.com/connect-iq/reference-guides/monkey-c-reference/).

Device selection matters. Garmin lists the fenix 8 AMOLED 47/51 mm family at
454x454, while the Solar 47 mm is 260x260 with a 64-color memory-in-pixel screen.
Our fixed 466px hitboxes need a logical layout transform and button-focus
navigation in addition to touch support on applicable models. These are examples,
not a recommendation to buy a device. [Compatible devices](https://developer.garmin.com/connect-iq/compatible-devices/).

Application.Storage supports persisted values with a 32 KB per-value limit.
Our small logical save state should fit comfortably, but its codec and migration
behavior need a Garmin implementation. That limit is not a claim about the total
app memory budget. [Storage API](https://developer.garmin.com/connect-iq/api-docs/Toybox/Application/Storage.html).

# Proposed Work Breakdown

1. Select one supported model; install the SDK/device package and create a device
   app. Render one pet and exercise touch/buttons in the simulator and on hardware.
2. Add a resource export target for our palette PNGs, fonts, and creature tuning
   profiles; adapt ring geometry and hit testing to the selected screen.
3. Port the deterministic simulation and save/resume logic to Monkey C. Run the
   same input/time fixtures against C and Garmin implementations to detect drift.
4. Add gestures/button focus, Moments, clock suggestions, and optional low-count
   particles. Profile runtime memory, redraw cost, and battery behavior on-device.
5. Replace USB debug assumptions with Garmin simulator/device diagnostics and
   supported app settings; our existing ESP32 serial protocol is not a ready-made
   Garmin transport. Package, sideload, test lifecycle/updates, then prepare a
   Connect IQ Store submission if requested.

Garmin documents SDK project resources, simulator testing, and copying compiled
PRG files to GARMIN/APPS for sideloading. [First app guide](https://developer.garmin.com/connect-iq/connect-iq-basics/your-first-app/).

My rough engineering estimate for one experienced developer is a few days for a
single-model rendering/input feasibility spike, and several weeks for gameplay,
persistence, testing, and polish. This is an estimate, not a measured schedule;
target-model limits and desired parity are the major unknowns. An always-visible
pet/watch-face variant adds power-management and lifecycle work.

# Next Decision

Choose the actual Garmin model and whether the goal is an interactive app, a
passive watch face, or both. Begin with a simulator/hardware spike before committing
to a broad multi-device port. No Garmin SDK was installed and no port code was
created during this research.
