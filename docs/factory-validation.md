# Factory validation foundation

The `factory` profile is a standalone, non-destructive smoke image for Waveshare
ESP32-S3-Touch-AMOLED-1.75 SKU 31261. It does not compile the game core or assets,
open saves, initialize NVS, start Wi-Fi, or initialize display/audio. It is not a
production acceptance suite. Every run currently reports `acceptance: false`.

## Build, flash, capture, restore

Use the pinned local SDK. All paths are resolved from the repository by the
wrapper; generated factory configuration and images live in `build/esp32-factory`.
The normal and power profiles have separate configuration and output directories.
Close other serial clients before flashing or capture.

```sh
./scripts/esp factory
./scripts/esp ports
# Set JELLI_DEBUG_PORT from current discovery, then flash separately:
./scripts/esp factory -p "$JELLI_DEBUG_PORT" flash
./scripts/uv run --python 3.12 tools/debug/factory_capture.py \
  --port "$JELLI_DEBUG_PORT" --fixture bench-unattended \
  --output build/factory-run-001
# Restore the normal application after capturing results:
make esp-build
make esp-flash PORT="$JELLI_DEBUG_PORT"
```

Capture uses the same `Client`, serial setup and raw recorder as `jelli-debug`
and power experiments. It saves raw serial bytes, command/reply JSON, and a host
capture UUID, station label, port and timestamp. `observations.json` interprets
only complete passing inventories and explicitly preserves RTC/gauge uncertainty. Each request has a three-second
deadline; completion polling has a ten-second deadline. The tool submits one
run request without retrying it. Exit **2** means completed with incomplete
coverage; exit **1** means an error, malformed results, transport failure or
timeout. This smoke schema never grants production acceptance.

## Shared debug contract

All profiles use `@J1 REQUEST_ID COMMAND` followed by LF and correlated JSON
replies. The portable parser owns bounded request/reply framing,
malformed-line handling and secret-buffer clearing. See
[the shared protocol limits](../include/jelli/debug_protocol.h) and
[parser implementation](../core/debug_protocol.c) for framing details. ESP32 profiles use
the same USB transport, bounded polling and reply timeout. SDK logs stay outside
framed replies. The former `@JF1` stream is removed; historical captures remain
in the original experiment evidence.

Discover a profile before choosing commands:

```sh
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" capabilities
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" factory tests
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" factory status
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" factory run
# Or isolate one test:
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" factory run pmic.read
# Use the run ID returned by the device, then query until status is no longer running:
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" factory status
./scripts/jelli-debug --port "$JELLI_DEBUG_PORT" factory result RUN_ID pmic.read
```

`capabilities` reports protocol 1, compiled profile/board, supported command
names and factory schema **3**. Game commands are absent from factory firmware;
`state`, `capture`, and care commands return `unsupported_command`. Station
state checks use `factory status`, which has factory meanings rather than game
state fields. Normal/power firmware retains its existing game-state contract.

`factory tests` lists stable IDs, whether each is supported without a fixture,
and whether it is required for acceptance. `factory run [all|TEST_ID]` queues a
run and returns its ID immediately. The main task executes at most one bounded
probe between command polls. A second run while running returns `busy` without
changing results. `factory status` exposes idle/running/incomplete/error, scope,
counters and identity. `factory result RUN_ID TEST_ID` queries a retained result;
unselected tests report `not_run`. Results persist until a new explicit run or
reboot. Old run IDs are rejected. Queries never start tests.

Status includes a boot nonce, silicon base MAC and application ELF SHA256. The
nonce distinguishes ordinary reboots; it is not a security token. The station
checks identity and run consistency during polling and brackets result reads
with status queries so a replaced run cannot silently pass. There is one serial
owner/station at a time. After a lost action reply, query status before deciding
whether to start another run. Neither the client nor capture tool retries runs.

The ELF hash identifies the running build, not the full flashed binary; retain
binary hashes and flash logs separately. The compiled board label is not proof
of physical SKU/revision; a production fixture must verify that independently.
The shared client also supports local socket transports, but factory firmware
itself has no network endpoint. Remote automation runs on the USB-connected
station host.

Results use stable IDs, `pending`, `not_run`, `pass`, `error` or `skip`, SDK
error code and elapsed microseconds. Pass meanings are intentionally narrow:

| Test ID | Current check | Pass establishes |
| --- | --- | --- |
| `identity` | Read base MAC from eFuse | Read succeeded; does not validate SKU or fixture inventory |
| `psram.scratch` | Pattern checks in a scratch region | That region passed; not full RAM coverage |
| `pmic.read` | Read selected AXP2101 registers | Register transactions succeeded; not rail correctness |
| `pmic.inventory` | Read allowlisted AXP configuration/status registers | Transactions succeeded; no rail, charging or gauge accuracy claim |
| `imu.identity` | Read QMI8658 identity, revision and selected controls | Transactions succeeded and identity matched; not motion accuracy |
| `rtc.snapshot` | Coherent PCF85063 calendar and timer-control snapshot | Transactions succeeded; not trusted UTC or retention |
| `display.visible` | Skip: external observation required | Nothing |
| `touch.physical` | Skip: physical stimulus required | Nothing |
| `audio.audible` | Skip: acoustic fixture required | Nothing |
| `power.current` | Skip: independent power/current fixture required | Nothing |

Inventory results contain the I2C `address`, ordered `[register,value]` `samples`,
and `valid` prefix length. Failed/unread bytes are not measurements. RTC calendar
uses one burst for a coherent sample; PMU/IMU inventory is a sequential snapshot.
No probe enables sensors, clears IRQ status, writes charging/rail settings or
sets time. Reading an address pointer is the only I2C write phase. IMU status/FIFO
registers are excluded. Reads are bounded and stop on first error. Current
register selections and timeouts live with the
[board probes](../ports/esp32/main/validation/boards/).
Schema 3 updates the catalog and required host validation; old schema 2 captures
remain historical evidence and are rejected by the new station tool.

Failed reads leave individual value slots zero; never interpret measurements
whose test did not pass. The status error count includes failed probe operations.
Required skipped or unselected checks prevent acceptance. The current schema remains
`incomplete` even if future code unexpectedly reports every check passing;
production acceptance requires an explicit reviewed schema/requirements update.

## Extension pattern

1. Add chip transactions to a named board probe module with explicit ownership,
   bounded waits, documented read/write effects, and structured return values.
   Keep USB formatting, game state and test policy out of shared probes.
2. Keep investigative sequences in `validation/power/`; add acceptance sequences
   to `validation/factory/`. Promote measured driver fixes into normal board
   support independently. Do not make the normal engine call factory tests.
3. For another board, define SKU/revisions, pins, buses and capabilities in its
   own board module/profile. Add an explicit build/CI entry and host identity
   support. Do not infer compatibility from a related product name.
4. Define a stable test ID, prerequisites, stimulus, units, timeout, cleanup,
   pass criteria, and skip/error behavior before adding a test to a suite.
   Hardware absence or unavailable fixture capability must never count as pass.
5. Add malformed/partial transcript tests and compile every supported profile
   in CI. Record bench evidence separately from physical or electrical proof.
   Version incompatible protocol or acceptance changes.

The factory profile keeps scratch and result state for the image lifetime and
does not add a task or framebuffer. Its memory layout is owned by the factory
and shared protocol implementation. Tests allocate nothing per run. Transport
loss leaves retained results available for queries; it does not trigger another
run.

See [validation layout](../ports/esp32/main/validation/README.md),
[ADR-013](../docs-cms/adr/adr-013-board-validation-profiles.md), and
[power evidence](../docs-cms/memos/memo-044-device-power-experiments.md).

The impact-ranked validation methodology is in [device validation passes](device-validation-passes.md).
