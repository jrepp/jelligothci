---
title: On-device Settings with Wi-Fi setup, and screen sleep, for the coding agent
status: Accepted
created: 2026-10-10T12:32:54Z
deciders: Michael Weingartner
tags: [agent, esp32, ui, wifi]
id: adr-016
project_id: jelligotchi
doc_uuid: 002b13d5-1e4f-41e1-8d93-11a5919d766f
---

# Context

Loki Agent (RFC-003) joins Wi-Fi only through the USB console (`scripts/loki-agent
wifi set`), so moving the board to another network needs a computer. Its screen
shows the unicorn all the time, which wastes power and wears the AMOLED.

The user asked, on 2026-10-10: "There are two buttons on the side of the device.
One is power, the other seems to be programmable. Can we use it to bring up a
preference screen and add to that the ability to list and select a wifi network, put
in the password, and connect to wifi? We also need to add the screen sleep after two
minutes and wake on screen double tap."

Facts that shape the answer: the 1.75C's side buttons are PWR, read on GPIO3 and
reserved by RFC-003 for "cancel" clicks (holding it powers the board off), and
BOOT, GPIO0, which the BSP does not expose (memo-024). The agent draws the panel
itself, without LVGL (memo-026), and had no touch input yet. The pets' 8x12 pixel
font has capitals only. The pet app already sleeps its screen after idle time and
wakes on a double tap (memo-023, `core/pet_screen.c`).

# Decision

## Decided by the user

The programmable side button opens a Settings screen, which lists Wi-Fi networks,
takes a password, and connects. The screen sleeps after two idle minutes and wakes
on a double tap.

## How it is built

Claude proposed the design below on the same day.

**The button.** BOOT (GPIO0, `JELLI_BOARD_BOOT_KEY_GPIO` in the 1.75C's board
wrapper) is read every UI tick, debounced over two samples. A press opens Settings,
or closes it; on a dark screen it only wakes. PWR keeps its RFC-003 role.

**Settings** is portable C in `core/agent/prefs*.c`: a state machine over taps,
drags, and the host's Wi-Fi results that draws itself, tested on the desktop with a
fuzz pass and BMP previews of every screen. Its screens: Settings (the network in use
and whether the board is online, and Done); Searching; the networks nearby
(deduplicated by name at their strongest signal, strongest first, at most 16, a lock
for those that need a password, signal bars, an outline on the one in use); the
password keyboard; Joining; and the outcome. It is drawn in the unicorn's style:
black, white text in the pets' pixel font with new lowercase letters (a password's
case matters), lilac for what is chosen or pressable, grey for what is not, and
words or outlines beside every colour. Everything lies inside the round panel with
targets at least 37 by 42 pixels.

**The keyboard** has four pages (letters, capitals, digits with common symbols,
the other symbols) reaching every printable ASCII character a WPA passphrase may
hold, a space, a delete, and Join, which counts down the characters still needed
until there are eight. Show reveals the typed password; it is dots otherwise.

**Joining** runs on a task of its own, since a scan or a join takes seconds. The
board tries the network before keeping it: only when it gets an address are the
credentials stored in `agent_nvs`, exactly as the console's `wifi set` stores them;
otherwise the stored network is reconnected, so a mistyped password never costs the
working one. A failure is told as "Wrong password", "Not found", or "Couldn't
connect" from the station's reason, and only an address on the network asked for
counts. Enterprise networks are listed but refused as "Not supported": they need a
login the board cannot do. Hidden networks are not listed.

**Look-alikes.** Anyone nearby can broadcast an open network named like the user's.
An open and a locked network of one name are therefore listed apart, the board's own
is outlined only when its lock matches the stored one, and joining an open network
named like the board's locked one first warns "Not locked. Join anyway?", with Back
as the bright button.

**Staying online.** When the station loses its network it retries five times
quickly, then once every 30 seconds for as long as it is offline, so a router that
restarts is found again. Only the board's own disconnects skip a retry: the same
"leaving" reason from the network, real or forged, is retried, and while a network is
stored and the board is offline the slow retry is always armed. Each slow retry
reloads the stored network into the driver, on a small task of its own; a network
change (a join, `wifi set`, `wifi clear`) holds off every retry until it is done,
and a failed join empties the driver of what was typed before going back.

**The password** lives in Settings' state only while it is typed. A join copies it
into the request and wipes it there; the request is copied into the Wi-Fi task's
single slot and wiped; that slot and the driver configuration's stack copy are
wiped once the join is done; closing Settings, Cancel, and screen sleep wipe it too.
It is never logged, drawn as other than dots unless Show is on, or reported over the
console.

**Screen sleep** reuses the pet's tested policy, now with the idle time as a
parameter: two minutes for the agent. A touch, a press, or the bridge's moments that
need the user (listening, a question, a spoken reply) keep it on; background progress
does not. Asleep, the panel shows black with its brightness at zero, Settings is
closed, and only a double tap (two taps within 500 ms and 80 px), the button, or such
a bridge moment wakes it; the waking taps press nothing.

**Touch.** The BSP's CST9217 driver, read every 40 ms tick by the UI task, in the
orientation the pet's LVGL input uses. A press that stays within 14 px and lifts
within 800 ms is a tap; one that moves further is a vertical drag.

The console gains `ui button`, `ui tap X Y`, `ui drag up|down PX`, and `screen
status|sleep|wake`, so the flow can be driven and checked without a finger. They give
nothing the USB cable does not already give, and never carry a password.

# Consequences

## Positive

- The board moves between networks without a computer, and a wrong password never
  strands it.
- The screen is dark most of the time, saving power and the AMOLED.
- Touch input exists now, ready for the on-screen hold-to-talk RFC-003 plans.
- Settings' logic and drawing are portable and tested on the desktop.

## Negative

- The stored password stays in `agent_nvs` in plain text, as the console's `wifi set`
  already stored it; flash encryption would be a separate decision.
- A scan briefly takes the radio, so a voice exchange during one can stutter.
- Hidden networks and enterprise logins cannot be joined from the board.
- About 11 KB more internal RAM (the 4 KB Wi-Fi errand and 3 KB slow-retry task
  stacks, and scan records), all static; 73 KB stayed free on the 1.75C.

## Neutral

- An open look-alike of the user's network can still be joined, but only past a
  warning; its row shows no lock and no outline, unlike the real one.
- The console's `wifi set` remains, for setup before the board has a screen.

# Alternatives Considered

## LVGL for Settings

The BSP brings LVGL, with lists, a keyboard, and smooth fonts. The agent's own
presenter owns the panel, its DMA stripe, and the transfer callback, so handing the
panel to LVGL and back would need a second ownership scheme for the same bus, and
the screens would not be testable without hardware.

## Storing the credentials first, as `wifi set` does

Simpler, but a mistyped password would replace a working network and leave the
board offline until someone fixed it by cable.

## PWR for Settings

PWR is RFC-003's cancel key, and holding it powers the board off; a settings
gesture on it would be one long press away from a shutdown.

# References

- [RFC-003: Voice agent companion](../rfcs/rfc-003-voice-agent-companion.md)
- [memo-023: Screen sleep and double-tap wake](../memos/memo-023-screen-sleep-and-double-tap-wake.md)
- [memo-024: Two apps and hardware probes](../memos/memo-024-agent-phase0-two-apps-and-hardware-probes.md)
- [memo-026: Render spike](../memos/memo-026-agent-phase0-render-spike.md)
