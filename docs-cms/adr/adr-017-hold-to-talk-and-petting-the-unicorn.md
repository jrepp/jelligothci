---
title: Hold the screen to talk to the bridge, and stroke the unicorn to make it frolic
status: Accepted
created: 2026-10-10T16:36:11Z
deciders: Michael Weingartner
tags: [agent, audio, esp32, ui]
id: adr-017
project_id: jelligotchi
doc_uuid: 180cc726-d922-46d8-bc13-8299c2610e8b
---

# Context

Loki Agent sends speech to Loki Bridge as an utterance: `ptt_start`, MIC audio frames,
and `ptt_end` ([RFC-003](../rfcs/rfc-003-voice-agent-companion.md)). Until now only the
serial console (`loki ptt start|stop`) could start one, so the board had no way to talk
to the bridge by itself. Michael asked to "require the user to press and hold the screen
to send an audio command to the bridge", and that "if the user 'pets' the unicorn on the
screen it should frolic around the screen in joy."

The unicorn's screen already takes taps (a double tap wakes it from sleep) and Settings
takes taps and vertical drags ([ADR-016](adr-016-on-device-settings-wifi-and-screen-sleep.md)).

# Decision

**Hold to talk.** On the unicorn's screen, a press that stays within the tap slop
(14 px) for 400 ms is a hold. The hold starts an utterance, and lifting the finger ends
it; the finger may wander while held. Speech reaches the bridge only while a finger is
held this way (or the developer console asks), never otherwise. An utterance still stops
at 30 seconds. Holds are off in Settings and on a dark screen, so Settings keeps its
slow taps (up to 800 ms) and a dark screen still wakes only to a double tap. Whether a
press may become a hold is decided when the finger lands: a press that began in Settings
or on a dark screen never becomes one when the screen wakes or Settings closes. Opening
Settings or the screen going dark ends an utterance, since neither shows the ring, and if
the touch controller stops answering for 120 ms during a hold the finger counts as
lifted.

**Feedback.** While an utterance runs, a lilac ring 8 px wide lines the round screen's
edge. When a hold cannot start (the bridge is offline, or the last utterance has not
ended), a grey ring blinks twice over 900 ms instead, so the two differ in motion and
not only in colour.

**Petting.** A drag on the unicorn's screen whose finger travels at least 120 px
(across plus down, each sample from the moment it leaves the slop) is a stroke that
pets the unicorn. When the finger
lifts, it plays a new routine, the frolic: it rears, leaps right twice, turns, leaps back
across twice, trots to the far side and rears there, turns, leaps home, and rears again,
in 5.45 s. It follows the routines' existing rules (one ground line, facing the way it
moves, turning only while standing, inside the round panel), which the prance tests
check for every routine.

The touch sampler (`core/agent/touch.c`) reports the gestures (HOLD, RELEASE, STROKE
beside TAP and DRAG); the board's controls decide what each means on the screen it lands
on. The console gains `ui hold start|stop`, `ui pet`, and `ui prance frolic`, and
`screen status` reports `listening`.

# Consequences

## Positive

- The board can start a voice task by itself, and only while a person is visibly
  holding it, so it never listens unexpectedly.
- Holding stops a reply that is playing (barge-in), as the console's `ptt start` did.
- Petting gives the unicorn a reason to be touched beyond Settings.

## Negative

- Speech starts 400 ms after the finger lands, so words spoken before the ring appears
  are lost. The ring is the cue to start speaking.
- A press on the unicorn held between 400 and 800 ms used to be ignored and is now a
  short utterance, which the bridge may answer with "didn't catch that".

## Neutral

- A drag on the unicorn's screen now ends with a STROKE event; Settings ignores it.
- The PWR key's cancel gesture from RFC-003 is unchanged and still unimplemented.

# Alternatives Considered

## Tap to start, tap to stop

One tap starts listening and another stops it. Rejected: a stray tap would leave the
microphone open, and Michael asked for press and hold.

## The side button

BOOT already opens Settings (ADR-016), and a hold on it would be invisible from the
front of the board.

## Starting the utterance at touch-down and discarding it on a short press

Would keep the first 400 ms of speech, but every tap on the unicorn would briefly open
an utterance at the bridge. Rejected for now; the 400 ms can be revisited if the clipped
start proves a problem.

# References

- [RFC-003: Voice agent companion](../rfcs/rfc-003-voice-agent-companion.md)
- [ADR-016: On-device settings, Wi-Fi and screen sleep](adr-016-on-device-settings-wifi-and-screen-sleep.md)
- [memo-028: Settings, Wi-Fi and sleep on the 1.75C](../memos/memo-028-agent-settings-wifi-and-sleep-on-hardware.md)
