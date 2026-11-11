"""Regenerate the original compact PCM bank; no external recordings or melodies."""
import argparse
import math
import random
from pathlib import Path


LEAD_NAMES = ('keys', 'classical', 'country', 'metal', 'kpop', 'house', 'eighties')


def lead_samples():
    """Single-cycle, harmonic-limited timbres; runtime supplies note envelopes."""
    leads = [[] for _ in LEAD_NAMES]
    for i in range(128):
        phase = 2 * math.pi * i / 128
        sine = math.sin
        tones = (
            72*sine(phase) + 25*sine(2*phase) + 12*sine(3*phase),
            64*sine(phase) + 19*sine(3*phase) + 10*sine(5*phase),
            55*sine(phase) + 26*sine(2*phase) + 16*sine(4*phase) + 9*sine(7*phase),
            70*sine(phase) + 28*sine(3*phase) + 19*sine(5*phase) + 13*sine(7*phase),
            56*sine(phase) + 28*sine(2*phase) - 17*sine(3*phase) + 12*sine(6*phase),
            66*sine(phase) - 27*sine(2*phase) + 15*sine(3*phase) - 9*sine(4*phase),
            58*sine(phase) + 24*sine(3*phase) - 16*sine(4*phase) + 12*sine(5*phase),
        )
        for lead, tone in zip(leads, tones):
            lead.append(round(tone))
    return leads


def rhythm_samples():
    """Small original one-shots and looped basses, independent of the legacy seeds."""
    phase = 0.0
    tight_kick = []
    for i in range(512):
        phase += 2 * math.pi * (75 + 150 * math.exp(-i / 55)) / 22050
        envelope = min(1, i / 8) * (1 - i / 511) ** 2
        tight_kick.append(round(112 * math.tanh(2.2 * math.sin(phase)) * envelope))
    metal_hat = []
    ratios = (1.0, 1.47, 1.91, 2.57, 3.21, 4.13)
    for i in range(128):
        metal = sum(1 if math.sin(2 * math.pi * 2400 * ratio * i / 22050) >= 0
                    else -1 for ratio in ratios)
        envelope = min(1, i / 3) * (1 - i / 127) ** 2
        metal_hat.append(round(18 * metal * envelope))
    noise = random.Random(8703)
    clap, previous = [], 0.0
    for i in range(256):
        current = noise.uniform(-1, 1)
        high_pass = (current - previous) / 2
        previous = current
        burst = sum(math.exp(-(i - onset) / 23) for onset in (0, 34, 72, 116)
                    if i >= onset)
        envelope = min(1, i / 4) * (1 - i / 255)
        clap.append(round(100 * high_pass * burst * envelope))
    acid_bass = []
    for i in range(64):
        phase = 2 * math.pi * i / 64
        # Harmonic-limited pulse; no discontinuous sampled square-wave edge.
        pulse = sum(math.sin(k * phase) * math.sin(k * math.pi * .3) / k
                    for k in range(1, 10))
        acid_bass.append(round(66 * pulse))
    return dict(tight_kick=tight_kick, metal_hat=metal_hat, clap=clap,
                acid_bass=acid_bass,
                sub_bass=[round(108 * math.sin(2 * math.pi * i / 64)) for i in range(64)])


def sample_bank():
    rng = random.Random(731)
    leads = lead_samples()
    voices = {
        # Retain the original exported symbol and samples for existing consumers.
        'keys': leads[0],
        'bass': [round(82*math.sin(2*math.pi*i/64)+15*math.sin(4*math.pi*i/64))
                 for i in range(64)],
        'brush': [round(rng.randint(-70, 70)*(1-i/256)) for i in range(256)],
    }
    # Original synthesized one-shots at 22050 Hz: downward sine kick and
    # independently seeded noise/body snare. Both taper exactly to silence.
    phase = 0.0
    kick = []
    for i in range(1024):
        t = i / 22050
        frequency = 60 + 125*math.exp(-t*110)
        phase += 2*math.pi*frequency/22050
        envelope = (1-i/1023)**2
        kick.append(round(112*math.sin(phase)*envelope))
    noise = random.Random(1907)
    snare = []
    for i in range(512):
        t = i / 22050
        envelope = (1-i/511)**2
        snare.append(round((noise.uniform(-78, 78)+30*math.sin(2*math.pi*185*t))*envelope))
    voices.update(kick=kick, snare=snare)
    voices.update(rhythm_samples())
    assert all(-128 <= x <= 127 for voice in leads + list(voices.values()) for x in voice)
    assert sum(map(len, leads)) + sum(map(len, voices.values())) < 4096
    return leads, voices


def tracker_presets():
    """Return MOD-compatible samples without the library's browsing metadata."""
    return [preset['sample'] for preset in tracker_library()]


def tracker_library():
    """Original authoring sounds, independent of the shipped song/legacy bank.

    Melodic waveforms use 32 samples per cycle (approximately C2 at tracker C2).
    Percussion is synthesized at the PAL C3 playback rate. MOD retains the
    waveform and loop settings, not a WAV sample rate. All noise is seeded.
    """
    presets = []

    def add(name, values, volume, loop=False, category='Percussion'):
        sample = dict(name=name, pcm=[round(value) for value in values],
                      volume=volume, loop_start=0,
                      loop_length=len(values) if loop else 0)
        presets.append(dict(id=name.lower().replace(' ', '-'), name=name,
                            category=category, sample=sample))

    def tone(i, harmonics):
        phase = 2 * math.pi * i / 32
        return sum(amplitude * math.sin(harmonic * phase)
                   for harmonic, amplitude in harmonics)

    add('Velvet keys', [tone(i, ((1, 78), (2, 18), (3, 7))) for i in range(32)], 36, True, 'Keys')
    add('Hollow flute', [tone(i, ((1, 84), (3, 12), (5, 5))) for i in range(32)], 34, True, 'Leads')
    add('Warm reed', [tone(i, ((1, 62), (2, 22), (3, 13), (4, 7)))
                      for i in range(32)], 32, True, 'Leads')
    for name, harmonics, length, decay, volume in (
            ('Glass bell', ((1, 68), (3, 24), (7, 12)), 2048, 700, 40),
            ('Pocket pluck', ((1, 62), (2, 26), (3, 14), (5, 8)), 1024, 260, 40)):
        add(name, [tone(i, harmonics) * min(1, i / 8) * math.exp(-i / decay)
                   * (1 - i / (length - 1)) for i in range(length)], volume,
            category='Mallets' if name == 'Glass bell' else 'Keys')

    rate = 3546895 / 214
    phase = 0.0
    kick = []
    for i in range(1024):
        phase += 2 * math.pi * (52 + 170 * math.exp(-i / 45)) / rate
        kick.append(112 * math.sin(phase) * min(1, i / 4) * (1 - i / 1023) ** 2)
    add('Round kick', kick, 48)
    add('Wood rim', [(70 * math.sin(2 * math.pi * 1700 * i / rate)
                      + 35 * math.sin(2 * math.pi * 2390 * i / rate))
                     * min(1, i / 2) * (1 - i / 255) ** 3 for i in range(256)], 42)
    for name, length, seed, volume in (('Seed shaker', 512, 2241, 34),
                                       ('Air hat', 1536, 2242, 26)):
        noise = random.Random(seed)
        previous = 0.0
        values = []
        for i in range(length):
            current = noise.uniform(-1, 1)
            values.append(60 * (current - previous) * min(1, i / 4)
                          * (1 - i / (length - 1)) ** 2)
            previous = current
        add(name, values, volume)

    for name, category, harmonics, volume in (
            ('Deep sine bass', 'Bass', ((1, 104),), 38),
            ('Rubber bass', 'Bass', ((1, 80), (2, 16), (3, -10)), 36),
            ('Buzz bass', 'Bass', ((1, 60), (2, 24), (3, 16), (4, 9)), 32),
            ('Soft organ', 'Keys', ((1, 58), (2, 25), (4, 18)), 34),
            ('Dream keys', 'Keys', ((1, 70), (3, -18), (5, 8)), 32),
            ('Arcade lead', 'Leads', ((1, 68), (3, 20), (5, 12), (7, 7)), 30),
            ('Silver lead', 'Leads', ((1, 62), (2, -20), (4, 12)), 32)):
        add(name, [tone(i, harmonics) for i in range(32)], volume, True, category)

    for name, harmonics, length, decay, volume in (
            ('Thumb piano', ((1, 88), (3, 14), (6, 7)), 1536, 400, 38),
            ('Wood marimba', ((1, 90), (4, 17)), 1024, 280, 40),
            ('Ice chime', ((1, 58), (5, 24), (9, 12)), 2048, 850, 34)):
        add(name, [tone(i, harmonics) * min(1, i / 8) * math.exp(-i / decay)
                   * (1 - i / (length - 1)) for i in range(length)], volume,
            category='Mallets')

    for name, frequency, length, volume in (
            ('Low tom', 105, 1536, 44), ('High tom', 185, 1024, 40)):
        phase = 0.0
        values = []
        for i in range(length):
            phase += 2 * math.pi * frequency * (1 + .65 * math.exp(-i / 70)) / rate
            values.append((94 * math.sin(phase) + 12 * math.sin(2 * phase))
                          * min(1, i / 4) * (1 - i / (length - 1)) ** 3)
        add(name, values, volume)

    noise = random.Random(2243)
    add('Paper snare', [(noise.uniform(-65, 65) + 37 * math.sin(2 * math.pi * 190 * i / rate))
                        * min(1, i / 4) * (1 - i / 1023) ** 2 for i in range(1024)], 42)
    noise = random.Random(2244)
    add('Hand clap', [noise.uniform(-76, 76)
                      * sum(math.exp(-(i - onset) / 65) for onset in (0, 52, 105) if i >= onset)
                      * min(1, i / 4) * (1 - i / 767) ** 2 for i in range(768)], 40)
    add('Copper cowbell', [(64 * math.sin(2 * math.pi * 540 * i / rate)
                           + 35 * math.sin(2 * math.pi * 800 * i / rate))
                          * min(1, i / 4) * (1 - i / 767) ** 2 for i in range(768)], 36)
    return presets


def rows(values, indent='    '):
    return '\n'.join(indent+', '.join(f'{v:4d}' for v in values[i:i+16])+','
                     for i in range(0, len(values), 16))


def generate():
    leads, voices = sample_bank()
    source = ('#include "music_samples.h"\n'
              '/* Original synthesized PCM, generated by tools/audio/music_samples.py.\n'
              ' * Lead order: keys, classical, country, metal, K-pop, house, eighties.\n'
              ' * Keep generated table layout deterministic. */\n'
              '// clang-format off\n'
              'const int8_t jelli_music_leads[JELLI_MUSIC_LEAD_COUNT][128] = {\n')
    for name, values in zip(LEAD_NAMES, leads):
        source += '    { /* '+name+' */\n'+rows(values, '        ')+'\n    },\n'
    source += '};\n'
    for name, values in voices.items():
        source += f'const int8_t jelli_music_{name}[{len(values)}] = {{\n'
        source += rows(values)+'\n};\n'
    return source+'// clang-format on\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(generate())
