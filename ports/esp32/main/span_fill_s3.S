/* Diagnostic ESP32-S3 PIE fill, eight native RGB565 pixels per store.
 * a2: 16-byte-aligned destination, a3: positive blocks, a4: aligned pattern.
 * q0 is caller-clobbered; no special shift/accumulator state is changed.
 * Scalar wrapper handles prefix/tail, so no vector overread or overwrite. */
    .text
    .align 4
    .global jelli_span_fill_blocks
    .type jelli_span_fill_blocks,@function
jelli_span_fill_blocks:
    entry a1, 32
    ee.vld.128.ip q0, a4, 0
    loopnez a3, .Lfill_end
    ee.vst.128.ip q0, a2, 16
.Lfill_end:
    retw.n
    .size jelli_span_fill_blocks, .-jelli_span_fill_blocks
