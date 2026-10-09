---
title: Wi-Fi setup, automatic time, OTA, and optional network debug
status: Draft
author: Codex
created: 2026-10-08T15:36:41Z
tags: [debug, embedded, ota, settings, wifi]
id: rfc-003
project_id: jelligotchi
doc_uuid: 83a62b73-04b6-4fc5-9d20-302e54fd074b
---

# Summary

Add a WIFI SETUP button to Settings. It starts a temporary protected Wi-Fi access
point (AP), shows a fresh random password on the device, and lets a phone or
computer open a local settings page. That page configures the home Wi-Fi network,
timezone, automatic clock sync, OTA updates, and optional debug access over Wi-Fi.
The user requested this flow and a Sol subagent to plan it. This document proposes
the details; it does not claim that the AP, page, or network debug is implemented.

The first increment uses USB debug and device tweakables to configure station
Wi-Fi, automatic time, timezone rules, and explicit HTTPS OTA pulls. The later AP
page must reuse those settings and operations, rather than introduce a second
configuration store. Keep gameplay and rendering independent of network access.

# Motivation

The panel supports touch navigation but has no keyboard for network credentials.
A temporary AP makes setup possible with an ordinary browser. A randomly generated
password shown on the panel gives the person holding the device access to setup.
USB remains useful for development and recovery. After setup, automatic UTC sync
and timezone rules remove routine clock and daylight-saving adjustments.

# Detailed Design

## Device flow and AP lifetime

Reserve an unused Settings slot for WIFI SETUP, preserving existing slot IDs.
Add a host capability flag so SDL can show a clear unavailable state or a preview
without pretending to run ESP32 Wi-Fi. The core emits a setup request and renders
bounded host-provided status. The ESP32 port owns all SDK calls and AP state.

After tapping WIFI SETUP, show STARTING, then a dedicated setup view containing
the SSID, password, local address, remaining time, and a large DONE control.
Use an SSID such as Jelli-Setup-AB12 with a short device suffix; the suffix is an
identifier, not a secret. Start with a 16-character password from a 32-character
alphabet that omits ambiguous glyphs. That gives 80 bits of password entropy.
Display it in four groups while showing that spaces are omitted when typing.
Ensure all text fits the safe central area of the 466×466 round display.

Generate passwords with the SDK entropy API, never the pet's deterministic random
state, a timer, or a MAC-derived value. Start Wi-Fi in station mode without opening
an AP, keep its RF subsystem active for entropy collection, then generate the
password and configure WPA2-PSK before enabling AP mode. Do not briefly publish
an open AP. The pinned SDK requires an active entropy source for a guaranteed
random stream; verify this sequence on hardware with modem sleep disabled during
collection. See the [SDK random API source](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/docs/en/api-reference/system/random.rst).

Proposed defaults are five minutes without a valid authenticated action and a
fifteen-minute absolute session limit. Valid settings requests refresh the idle
deadline; scans, rejected login attempts, and other traffic do not. Start AP with
one associated client. DONE, timeout, reboot, and initialization failure end the
session, revoke browser sessions, clear secret buffers, and return to normal
station mode or Wi-Fi off. A subsequent setup session gets a new password.
Do not start setup automatically after boot or station failures.

## AP and station coexistence

Use AP+station mode during setup so current station service can remain available
and new credentials can be tested before replacing the saved network. Configure
the setup network at 192.168.4.1/24 with DHCP. Detect overlap with the station
subnet and choose a documented fallback subnet; display the actual address.
The setup AP provides access to the device, without routing or NAT to the home
network or internet. Bind or explicitly restrict setup handlers to the AP
interface, even if the server implementation listens on multiple interfaces.

The ESP32-S3 has one radio channel for both interfaces. Joining a new home network
can move the AP's channel and temporarily disconnect the phone. Return the test
operation ID before starting the join, keep the AP password and operation status
stable, and let the browser resume polling after reconnect. This follows the
[SDK AP/station channel behavior](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-guides/wifi.html#home-channel).

Keep one candidate credential set in RAM. Test association and DHCP with a
30-second deadline; success does not require internet access. On success, commit
the candidate to the shared network store and report clock sync separately.
On failure, discard the candidate and reconnect using the last saved network.
An NVS commit failure keeps the old saved configuration and reports that the new
connection is temporary. Power loss before commit restores the last saved set.
Opening AP setup does not erase credentials or pet saves.

## Browser settings and authentication

Serve a small embedded page with no CDN, third-party script, or external assets.
Show the full local URL on the panel, initially http://192.168.4.1/. Captive-portal
discovery can be added later; explicit browser access is the initial requirement.
The page explains that the setup network has no internet access.

Require login using the displayed password before returning settings or accepting
changes. Rate-limit login attempts. Use a freshly random session cookie with
HttpOnly and SameSite=Strict, a session-bound CSRF token for POST operations, and
strict Host/Origin validation. Never put credentials in URLs. AP-only HTTP relies
on the WPA2 link and the short physical setup session; restrict it to that
interface and never reuse the AP password for normal LAN debug access.

The page includes these controls:

| Control | Proposed behavior |
| --- | --- |
| Home Wi-Fi | Bounded scan list plus manual SSID entry for hidden networks; password field is write-only |
| Test and save | Try candidate credentials, then commit on successful DHCP; show separate connection and save results |
| Automatic time | Enable or disable periodic SNTP; show last successful sync and RTC availability |
| Timezone | Named presets with daylight-saving rules; custom validated rule or fixed offset for advanced use |
| Firmware update | Show current version and OTA status; accept HTTPS pull URL and explicit Start update |
| Restart into update | Available only after staging succeeds; checkpoint the pet before restarting |
| Wi-Fi debug | Disabled by default; enable explicitly and pair the debug app as described below |
| Forget home Wi-Fi | Clear only network credentials after a page confirmation; stop station service |
| Done | End setup and return to the pet |

Escape SSIDs, errors, and all other device strings when rendering HTML. Reject
unknown fields, duplicate keys, excessive lengths, malformed encodings, and
unsupported Wi-Fi security types. Initial station setup supports personal
networks, with open or enterprise networks left outside this setup increment.
Password omission means preserve the existing secret; explicit replacement and
forget are separate operations. Never return a saved password to the browser.

## Shared settings, storage, and tweakables

Keep network configuration in its existing dedicated NVS namespace, separate
from the game checkpoint. Version and explicitly encode the bounded record.
Use the same validation and apply path for USB debug, device tweakables, and the
web page. Secrets require typed write operations and a redacted status response;
they are not readable gameplay tunables or per-pet overrides. Preserve unsupported
records and report them instead of resetting the partition.

Extend the record in a new schema with the Wi-Fi debug enable flag and paired
client credential verifier. Keep AP password, browser cookie, candidate Wi-Fi
password, and unfinished operations in RAM only. Configure the Wi-Fi driver to
use RAM configuration so the application NVS record remains the authority and
the driver does not keep another unmanaged persistent copy of credentials.

Current development NVS should not be described as encrypted at rest. A later
production security decision may enable NVS encryption and key provisioning;
that is separate from this AP feature and must account for USB recovery. Logs,
generic debug state, configuration listings, event streams, and screenshots
must not expose credentials. Disable framebuffer capture while the setup
password or pairing secret is visible; a redacted configuration reply alone
does not protect a framebuffer containing the password.

## UTC sync and timezone behavior

SNTP supplies UTC; timezone selection is a separate setting. Offer friendly
timezone names backed by a small reviewed POSIX-rule table, retaining fixed
offsets for compatibility. Selecting the region once is sufficient for the
rule's future daylight-saving transitions. It does not automatically detect
travel or future legal rule changes. Avoid silently inferring location from IP.

Keep the board RTC in UTC. Compute the effective offset in the ESP32 port and
inject it into the existing signed-minute UI/game contract. Use the SDK's
timezone support outside the core; serialize changes to its process-global TZ
state. The SDK documents periodic SNTP and rule-based local time separately in
[system time](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/system/system_time.html).

Network callbacks publish a bounded time mailbox; the engine task applies it
through the existing session clock path. Continue simulation using injected
monotonic elapsed time. UTC corrections, backward clock changes, timezone
selection, and daylight-saving transitions must not advance care twice, shorten
cooldowns, or manufacture an offline interval. Preserve the RTC trust sequence
that revokes validity before a partial write can be mistaken for a valid clock.
When offline, retain the last valid RTC and selected timezone rule. Report the
last network sync separately from RTC known/unknown.

## Optional debug support over Wi-Fi

Wi-Fi debug is off by default and remains off until explicitly enabled through
authenticated setup or USB configuration. Turning it off closes clients, revokes
sessions, and releases any capture owned by that connection. Display a visible
debug-enabled indicator and provide a quick disable action in Settings.

Propose an HTTPS endpoint on the station interface using a device certificate
whose fingerprint is paired from the setup session. The app pins that certificate
and uses a separate 256-bit random bearer credential, delivered once through a
short pairing step while connected to the protected AP. Store a cryptographic
verifier of that credential, compare it without secret-dependent early exit,
and support explicit revocation/re-pairing. The setup password never becomes the
LAN debug credential. Require authenticated requests for state, events, input,
cheats, tweakables, and pixels; do not expose an unauthenticated raw TCP console.
Do not fall back to plaintext LAN debug when TLS initialization fails.

Carry one existing @J1 command per authenticated HTTP POST and return its normal
framed reply. Preserve current request IDs, protocol version, size limits, and
the rule against automatic input retries. Add URL, pinned-certificate, and
credential-file options to the existing CLI/viewer so their current controls and
screenshot logic work through a transport adapter. A timeout does not prove that
an input or update was canceled. Poll status before choosing another action.

Keep all parser calls and engine access on the engine task. Network handlers
copy a bounded request into a fixed queue and consume copied replies. Use one
global debug/capture arbiter across USB and Wi-Fi, with per-connection parser
state and capture ownership. Other clients receive busy during an owned capture;
they cannot read its pixels or release its token. Disconnects release only their
own capture. Preserve the existing five-second idle and thirty-second total
capture limits and forgiven capture time. Exclude OTA staging and setup secret
display from capture; process clock/settings changes after the capture ends.

## OTA integration

The web page calls the same host operation that USB uses for an explicit HTTPS
pull. Download into the inactive OTA slot, retain TLS server verification, check
the image and device target, and stage only a valid complete image. Never accept
an arbitrary file write or expose USB flash functionality as an HTTP handler.
Report progress, failure, and staged state independently of the browser lifetime.

Separate staging from reboot. Before restarting, request a successful pet
checkpoint; a save failure leaves the device running with a staged update.
Keep OTA metadata, both application slots, and NVS consistent with the initial
network increment's partition map. Later updates must fit a slot and remain
compatible with retained saves. Mark a new image valid only after its required
startup health checks; retain the rollback path. Record whether those checks
verify startup only or also verify user-observed panel and touch behavior.
Credentials and authentication tokens must not appear in OTA URLs or logs.

## Resource and task budget

Keep Wi-Fi, HTTP/TLS, scanning, DNS, and OTA flash writes off the engine task.
Reuse the initial network worker where practical and account for SDK tasks
separately. No network task reads a live framebuffer or calls gameplay code.
The existing engine and LVGL RGB565 buffers total 868,624 bytes; preserve their
ownership and do not add a full screenshot buffer for this feature.

Start with these proposed bounds, then measure before merging implementation:

| Resource | Bound or evidence required |
| --- | --- |
| AP clients / web sessions | One AP station, one authenticated setup session |
| HTTP sockets | At most two; refuse extra clients; bound read and write deadlines |
| Settings request body | 1 KiB maximum; fixed scratch storage; reject chunked or oversized bodies |
| Scan results | At most sixteen displayed records; collect/free SDK results after each scan |
| Debug messages | Follow JELLI_DEBUG_LINE and JELLI_DEBUG_REPLY; initial network increment plans 512/4096 bytes |
| Debug queue | One request and one reply mailbox, one outstanding command per admitted client |
| Setup/config operations | One mutation in flight; busy response rather than unbounded queue growth |
| Worker/server stacks | Record configured bytes and measured high-water mark for each task |
| Wi-Fi/TLS RAM | Record minimum internal free heap and largest free block through connect, setup, and OTA |
| Flash | Report image size, per-OTA-slot margin, embedded page size, and NVS growth |
| Frame latency | Measure normal play, AP scan/join, slow client, screenshot, and OTA staging |

Fixed application buffers do not make the firmware heap-free. SDK Wi-Fi and TLS
may allocate at startup and during bounded network work. Limit counts and sizes,
handle failures, and document these allocations. Retain the LVGL display mutex
and touch queue. Do not change PMIC rails, charging, or low-power policies as an
implicit part of Wi-Fi setup.

# Migration Strategy

Ship USB-configured station Wi-Fi, SNTP, timezone presets, and staged OTA first.
The AP phase consumes that store and preserves existing saves and configured
networks. A missing debug flag defaults to false. Unknown newer config versions
remain untouched and disable configuration writes until understood. Changing
partition layout requires an explicit USB deployment/recovery plan; ordinary OTA
does not silently rewrite the partition table.

# Drawbacks

The AP and web/TLS tasks consume internal memory and power alongside LVGL. The
shared radio channel can disrupt a browser during credential testing. AP-only
HTTP does not provide TLS protection beyond the WPA2 link. LAN debug adds pairing
and certificate handling to the app. A limited timezone table needs maintenance
when regional rules change. Measurements and device acceptance remain necessary.

# Alternatives

USB-only configuration uses the current debug path and avoids a web server, but
requires a cable and developer tools. An always-on AP avoids explicit setup entry
but consumes power and leaves a permanent configuration surface. BLE or the SDK
provisioning protocol can provide a different pairing path, but needs dedicated
client support beyond the requested ordinary browser flow. A plaintext LAN debug
port is smaller but would disclose credentials and commands on the local network.

# Adoption Strategy

1. Establish and verify USB device settings, offline RTC behavior, timezone rules,
   staged OTA, explicit save-before-reboot, and rollback.
2. Add Settings entry, password display, AP start/stop, deadlines, and browser login.
3. Add bounded scan, candidate test/commit, timezone selection, and existing OTA
   controls using the shared configuration API.
4. Add authenticated HTTPS debug transport and app pairing, sharing capture
   ownership with USB. Keep disabled-by-default behavior through upgrades.
5. Update the playing/debug and hardware guides with setup, recovery, privacy of
   pairing secrets, and measured resource limits. Capture device findings in a memo.

Use deterministic host tests for state transitions, deadlines, config migration,
credential failure, redaction, and capture ownership. Exercise actual HTTP/TLS
integration for origin/CSRF rejection, malformed bodies, slow clients, disconnects,
and command delivery without duplicate input. Test DST transitions and clock jumps
without wall-clock sleeps in core tests. Compile affected ports with the pinned SDK.

On hardware, verify that the password is readable on the round panel, physically
tap setup and DONE, connect a phone and computer, test wrong and changed home
credentials, recover from AP channel migration, expire setup, and reboot at commit
boundaries. Verify saved pet state, offline clock retention, and explicit OTA
failure/rollback behavior. Check that unpaired LAN clients cannot inspect or
control the pet. Record heap, stack, frame latency, and battery observations.
Builds, renderer captures, serial startup, and physical tests are separate evidence.

# Unresolved Questions

The proposed AP timeouts and sixteen-character password need a usability check.
Choose the first supported timezone regions and the policy for maintaining their
rules. Confirm whether Wi-Fi debug enablement persists across boots or requires a
new grant each session; default-off on a fresh configuration is required either
way. The app's device-certificate pairing UI and secure credential storage remain
to be designed. Decide separately whether releases need signed firmware, encrypted
NVS, and certificate rotation before treating this prototype as a production update
system. None of these questions prevents the USB-first network increment.

# Future Possibilities

Add a captive portal, QR code pairing if the available fonts/layout allow it,
power-aware reconnect windows, and maintained timezone data. Automatic travel
timezone detection needs a separate location/privacy design. Remote internet
debug and unattended OTA are outside this local setup proposal.

# References

- [Portable engine contracts](../../include/jelli/engine.h)
- [Debug buffer and capture contracts](../../include/jelli/debug.h)
- [Debug design tradeoffs](../adr/adr-010-bounded-usb-serial-debug-interface.md)
- [Historical USB debug evidence](../memos/memo-009-wired-debug-interface-and-validation.md)
- [USB acceptance and tunables](../memos/memo-012-cli-acceptance-and-creature-tunables.md)
- [Deployment and evidence guidance](../memos/memo-004-process-learnings-and-context-remediation.md)
- [Existing RTC and checkpoint adapter](../../ports/esp32/main/session.c)