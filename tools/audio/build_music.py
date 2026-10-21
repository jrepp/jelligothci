"""Compile the supported four-channel MOD subset into seekable PCM spans.

PAL clock, 22050 Hz mono, integer nearest-neighbour PCM8 mixing. Timeline
uses rational tick timing; phase is Q16 and never depends on render blocks.
"""
from fractions import Fraction
from pathlib import Path
import argparse
import importlib.util
import struct

RATE = 22050
LENGTH = RATE * 30
KEYS = ('morning-tea', 'garden-waltz', 'porch-picnic', 'tiny-thunder',
        'neon-heart', 'cloud-club', 'pixel-sunset', 'pocket-arcade', 'garden-drift',
        'tea-steam', 'bubble-bath', 'moon-pillow', 'little-steps', 'bubble-rush')


def compile_song(doc):
    """Return four lists of (start, phase, step, instrument, volume)."""
    channels = [[(0, 0, 0, 0, 0)] for _ in range(4)]
    speed, bpm, order, row = 6, 125, 0, 0
    time = Fraction(0)
    # Fastest permitted timing is one tick at BPM 255: <3061 rows /30 s.
    for _ in range(3061):
        start = int(time)
        if start >= LENGTH:
            break
        for ch, cell in enumerate(doc['patterns'][doc['orders'][order]][row]):
            previous = channels[ch][-1]
            phase = previous[1] + (start - previous[0]) * previous[2]
            step, instrument, volume = previous[2:]
            if cell['sample']:
                instrument = cell['sample']
                volume = doc['samples'][instrument - 1]['volume']
            if cell['period']:
                step = round(Fraction(35468946 * 65536, 10 * cell['period'] * RATE))
                phase = 0
            if cell['effect'] == 12:
                volume = cell['param']
            elif cell['effect'] == 15:
                if cell['param'] < 32:
                    speed = cell['param']
                else:
                    bpm = cell['param']
            if cell['sample'] or cell['period'] or cell['effect'] == 12:
                span = (start, phase, step, instrument, volume)
                if start == previous[0]:
                    channels[ch][-1] = span
                else:
                    channels[ch].append(span)
        time += Fraction(RATE * 5 * speed, 2 * bpm)
        row += 1
        if row == 64:
            row = 0
            order += 1
            if order == len(doc['orders']):
                order = doc['restart'] if doc['restart'] < len(doc['orders']) else 0
    else:
        raise ValueError('MOD timing exceeded the bounded row budget')
    return channels


def render_stems(doc):
    """Four PCM16 LE stems; direct sum at unity equals native mono PCM."""
    channels = compile_song(doc)
    outputs = []
    for spans in channels:
        output = bytearray()
        index = 0
        for cursor in range(LENGTH):
            if index + 1 < len(spans) and spans[index + 1][0] <= cursor:
                index += 1
            start, phase, step, instrument, volume = spans[index]
            value = 0
            if instrument and step:
                sample = doc['samples'][instrument - 1]
                offset = (phase + (cursor - start) * step) >> 16
                loop = sample['loop_length']
                first_end = sample['loop_start'] + loop if sample['loop_start'] else len(sample['pcm'])
                if loop and offset >= first_end:
                    offset = sample['loop_start'] + (offset - first_end) % loop
                if offset < len(sample['pcm']):
                    value = sample['pcm'][offset] * volume
            output.extend(struct.pack('<h', value))
        outputs.append(bytes(output))
    return outputs


def render_pcm(doc, gains=(64, 64, 64, 64)):
    """Exact native-renderer reference: 30 seconds of little-endian PCM16."""
    if len(gains) != 4 or any(type(g) is not int or not 0 <= g <= 64 for g in gains):
        raise ValueError('Preview needs four gains in 0..64')
    stems = [struct.iter_unpack('<h', pcm) for pcm in render_stems(doc)]
    output = bytearray()
    for voices in zip(*stems):
        value = 0
        for voice, gain in zip(voices, gains):
            scaled = voice[0] * gain
            value += scaled // 64 if scaled >= 0 else -((-scaled) // 64)
        output.extend(struct.pack('<h', value))
    return bytes(output)


def build(root, output):
    spec = importlib.util.spec_from_file_location('music_mod', root / 'tools/jelli-art/music_mod.py')
    codec = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(codec)
    lines = ['/* Generated from bounded ProTracker MOD files. */',
             'typedef struct { uint32_t start; uint64_t phase; uint32_t step; uint8_t instrument, volume; } MusicSpan;',
             'typedef struct { const int8_t *pcm; uint16_t length, loop_start, loop_length; } MusicSample;',
             'typedef struct { const MusicSpan *spans; uint16_t count; } MusicChannel;',
             'typedef struct { MusicSample samples[31]; MusicChannel channels[4]; } MusicScore;']
    scores = []
    for song, key in enumerate(KEYS):
        doc = codec.decode((root / 'content/music' / (key + '.mod')).read_bytes())
        samples = []
        for i, sample in enumerate(doc['samples']):
            name = f'music_pcm_{song}_{i}'
            if sample['pcm']:
                lines.append(f'static const int8_t {name}[] = {{' + ','.join(map(str, sample['pcm'])) + '};')
            else:
                name = 'NULL'
            samples.append('{%s,%d,%d,%d}' % (name, len(sample['pcm']), sample['loop_start'], sample['loop_length']))
        channels = []
        for ch, spans in enumerate(compile_song(doc)):
            name = f'music_spans_{song}_{ch}'
            lines.append(f'static const MusicSpan {name}[] = {{')
            lines.extend('{%du,UINT64_C(%d),%du,%du,%du},' % span for span in spans)
            lines.append('};')
            channels.append('{%s,%du}' % (name, len(spans)))
        scores.append('{{' + ','.join(samples) + '},{' + ','.join(channels) + '}}')
    lines.append('static const MusicScore music_scores[] = {' + ',\n'.join(scores) + '};\n')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.root, args.output)
