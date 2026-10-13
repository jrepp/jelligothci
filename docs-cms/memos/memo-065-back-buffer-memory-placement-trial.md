---
title: Back-buffer memory placement trial
author: Codex
created: 2026-10-11
tags: [embedded, memory, performance, validation]
id: memo-065
project_id: jelligotchi
doc_uuid: 5128c489-7868-4c85-aa9c-92621d1b8ab6
---

# Scope and memory classes

The user requested continued hardware optimization and investigation of ESP32
memory classes for back-buffer transfers. This increment extends the
[LVGL attribution trial](memo-064-lvgl-update-cost-and-copy-trial.md) with actual
allocation classification, guarded copy measurements and matched partial-buffer
placement builds. It does not replace production buffer ownership.

| Memory class or capability | Relevance to this display |
| --- | --- |
| Internal byte-addressable DRAM | Suitable for pixel work; limited available capacity. Tested with small LVGL draw buffers. |
| Internal DMA-capable RAM | The current panel staging allocation. DMA is a capability, not a separate faster kind of physical SRAM. |
| External PSRAM | Holds the two complete frames and baseline LVGL draw buffers; cached and capacity-rich, with bandwidth/cache costs for larger working sets. |
| Instruction/32-bit-only memory | Not interchangeable with byte/halfword-addressed RGB565 and library copy paths. No back-buffer placement attempted. |
| RTC/retention memory | Intended for retained state, not selected for this bulk pixel pipeline. No throughput claim is made. |
| Flash | Read-only asset source during rendering, not a writable back buffer. |

Espressif documents capability-based allocation and the restrictions on
32-bit-only and DMA memory in its [allocation guide](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-reference/system/mem_alloc.html).
PSRAM shares cache resources and large accesses can exceed the cache working set;
external DMA also has descriptor and bandwidth restrictions. See the
[external-RAM guide](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32s3/api-guides/external-ram.html).
The SDK pin is in [toolchain.env](../../toolchain.env).

The pinned IDF SPI LCD path supports an explicit `psram_dma_direct` configuration
flag and maps it to `SPI_TRANS_DMA_USE_PSRAM`. The current BSP does not enable
that flag; simply passing a PSRAM pointer would not establish direct DMA and
can trigger driver bounce allocation. A direct-PSRAM-DMA experiment would need
separate alignment, cache, lifetime, contention and output qualification. This
trial retains the proven internal staging copy. The exact implementation is
[esp_lcd_panel_io_spi.c](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/esp_lcd/spi/esp_lcd_panel_io_spi.c).

# Bounded implementation

[display_memory_trial.c](../../ports/esp32/main/display_memory_trial.c) is linked
only into the power image. A startup wrapper copies the BSP adapter configuration
and adjusts draw-buffer height/placement according to
[Kconfig](../../ports/esp32/main/Kconfig.projbuild). The adapter copies that
configuration by value and retains ownership of both allocations. There is no
runtime buffer exchange, new task or new frame-loop allocation.

| Trial mode | LVGL partial buffers | Payload total |
| --- | --- | --- |
| 0, baseline | Two 50-row PSRAM buffers | 93200 bytes |
| 1, size control | Two 8-row PSRAM buffers | 14912 bytes |
| 2, placement candidate | Two 8-row internal buffers | 14912 bytes |

The engine and canvas frames remain separate guarded PSRAM allocations. Each
frame contains 434312 pixel bytes. All variants retain the same 46600-byte
internal DMA staging allocation, panel transport, swap path, copy implementation,
CPU/cache configuration and double-buffer completion ownership. Thus modes 1
and 2 compare placement at the same strip size; mode 0 supplies the current
pipeline baseline. Shrinking the staging allocation is a separate experiment,
since narrow updates can use more rows than the nominal full-width height.

The probe adds 2520 bytes of fixed internal scratch/report/timings plus two small
configuration fields, below a 2.5 KiB scratch assertion. It borrows existing
canvas and staging buffers while holding the display mutex after a checked
panel-command drain. Source pixels remain engine-owned and unchanged. The
canvas is restored and invalidated before unlocking. The largest copy leaves
four-byte guards at both ends of the staging allocation. Comparisons, guard
checks and setup run outside timing. There is no second internal strip solely
for a benchmark; that unsupported copy size is reported as null.

The main-task minimum stack reserve reached 560 bytes in one configuration
(592 in the others), below earlier profiler-only observations. This temporary
report/benchmark path has a stack-margin limitation and is not production
acceptance. Full networking/TLS/OTA, simultaneous audio and long-duration
allocation pressure were not stress-tested.

# Device method and recovery observations

Rediscovery identified USB serial 90:70:69:FE:21:DC at `/dev/cu.usbmodem2101`.
The initial normal `game` profile on SKU31261 reported healthy buffers/guards/heap
and zero mismatches/transfer failures. Each test image was separately built,
hashed and flashed with verified transfer hashes. Ignored
`build/memory-transfer-001` retains binaries/configurations, source evidence,
serial, summaries and separate build/flash logs.

The first host memory validation rejected a valid draw buffer because it assumed
the active stride always represented the full panel width. LVGL reshapes its
partial buffer for a narrow dirty rectangle; the captured stride was 88 bytes
while capacity remained 46600. The validator now checks positive stride within
capacity and sufficient allocation for the configured full-width strip. A host
test retains the narrow-stride case. The failed attempt was read-only; a fresh
run passed and the original failure log remains in evidence.

The internal variant's monitor-initiated reset produced USB_UART_CHIP_RESET,
app version 0.8.0, an 8 MB 80 MHz PSRAM device with its memory test passing,
panel creation, IRQ touch registration, pet ready and debug ready. A startup
I2S disable-before-enable error was followed by successful codec open and sound
ready; no audio-driver change was made in this display increment. This does not
establish physical audio acceptance. A touch-library gesture warning likewise
did not indicate failure of this application's own pointer/gesture path.

# Measured allocation headroom

The memory probe classified both full frames as external, the staging pointer
as internal and DMA-capable, and the active draw buffer in the requested memory
class. Internal draw buffers also reported DMA-capable, although the experiment
still copies them into the staging strip. Runtime readings after startup:

| Mode | Internal free bytes | Largest internal block | Historical minimum free |
| --- | --- | --- | --- |
| Baseline PSRAM | 41559 | 31744 | 39780 |
| Small PSRAM | 41683 | 31744 | 39780 |
| Small internal | 26783 | 18432 | 26436 |

These readings include initialized network/audio subsystems but do not guarantee
headroom under their peak workloads. A complete 434312-byte frame cannot fit in
the observed internal heap. Even moving both current 50-row partial buffers would
exceed the observed free space. This motivated the matched smaller-buffer trial,
rather than an allocation that depended on fallback or silently changing class.

# CPU copy results

Three runs per configuration each execute eight copies per supported size/path,
reversing path order on alternate rounds. Representative baseline medians in
microseconds per copy:

| Source to destination | 72 bytes | 932 bytes | 46592 bytes |
| --- | --- | --- | --- |
| PSRAM to PSRAM | 4.125 | 17.375 | 2469.750 |
| PSRAM to internal DMA | 3.875 | 16.000 | 799.125 |
| Internal DMA to PSRAM | 1.375 | 4.875 | 1445.750 |
| Internal scratch to internal DMA | 1.375 | 4.875 | Not allocated |

All copied bytes and guards passed in all nine runs. These are CPU `memcpy`
measurements, not DMA bus rates. Setup initializes the destination and, for
internal-source cases, copies the source outside timing. That affects cache
warmth differently across paths; no equal-cache-state or cold-cache bandwidth
claim is made. The small working-set results cannot predict a whole-frame gain.

# Live display comparison

Three ten-second captures per configuration requested full invalidation every
500 ms while the normal pet continued animating. This is a mixed-update workload,
not deterministic replay. The internal-image capture descriptions also disclose
that an operator was invited to check menus; no physical response had been
received during the captures. No interaction equivalence is assumed.
The station records placement with each capture and checks the timing identity,
exact interval accounting and display health. All passed with no mismatches or
transfer failures. Median per-capture p95 values:

| Interval | Baseline PSRAM us | Small PSRAM us | Small internal us |
| --- | --- | --- | --- |
| LVGL update | 82734 | 168127 | 160073 |
| Residual work per update | 52746 | 128031 | 120638 |
| RGB565 image draw per call | 2555 | 342 | 204 |
| RGB565 fill per call | 1247 | 153 | 90 |
| Flush callback per strip | 3254 | 711 | 681 |
| Flush wait per strip | 20 | 22 | 22 |
| Staging copy per strip | 1293 | 44 | 41 |

The same-sized internal buffers reduce update p95 by about 4.8% versus small
PSRAM buffers. Both are substantially slower than the current large-strip
baseline. Per-strip savings cannot be multiplied into an assumed end-to-end gain:
smaller strips require many more draw/flush operations. Image and solid-fill
calls together account for only about 7.7% of total LVGL time in the internal
variant, while residual work is about 75.5%. Those draw calls are subsets of
residual work. Its remaining time is not yet attributed; task dispatch/waits
are a candidate for further measurement, not an established cause.

# Decision

Retain the current normal-firmware configuration. The tested internal placement
is faster for its matched small-buffer control, but neither small-buffer variant
improves the measured full-update workload and the internal variant consumes
scarce heap. Keep the test-only modes and memory evidence for follow-up. Before
changing DMA ownership or moving full frames, measure the remaining dispatch
work and investigate whether a useful strip size fits the real memory budget.


# Validation and session completion

A final reflash of the baseline returned update p95 to 82765 us, consistent with
the original median of 82734 us. This argues against simple session drift as the
explanation for the small-buffer regressions. The power build setting was restored
to baseline; normal firmware retains its original BSP allocation path.

Normal and all three power configurations built. Normal/power/factory isolation
checks passed; the factory artifact was inspected, not rebuilt for this change.
Tracked formatting, clang-tidy, Cppcheck and size checks passed. The new C probe
received explicit formatting, Cppcheck and size checks. All 21 station experiment
tests passed, including memory-class, narrow-stride and malformed/missing timing
cases. No portable engine logic changed, so core/sanitizer suites were not rerun.
No physical display/touch response was received during this trial, and no external
current meter was available. Serial health and exact copy checks do not establish
physical panel output or energy savings.

Normal firmware was restored with verified transfer hashes. Its captured startup
showed a monitor-initiated USB reset, version 0.8.0, successful PSRAM test, panel
creation, touch registration and pet ready. Runtime checks then confirmed profile
`game`, intact buffers/guards/heap, three checks, zero mismatches/transfer failures
and 1340 bytes minimum main-task stack reserve. Clock trust remains unknown.
Serial and the monitor are closed; the internal-placement observation window has
ended without physical confirmation.

Documentation check/repair passed for 86 records. The only repair sorted this
memo's tags; it was reviewed. `git diff --check` passed. Progress and completion
were sent to the requested ntfy topic. No commit, merge or push was made.
