"""Author Iron Pulse, an original hard-techno four-channel workout MOD.

Explicit regeneration only. Twenty bars at160BPM fill the30-second engine loop:
16 bars of driving groove followed by a four-bar breakdown/build turnaround.
Compact instruments derive from the existing original synthesized PCM bank.
"""
import argparse
import math
from pathlib import Path
import sys
from music_samples import sample_bank

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'jelli-art'))
import music_mod


def compose():
    leads, bank = sample_bank()
    kick = [round(110 * math.tanh(v / 48)) for v in bank['kick']]
    bass = [round(105 * math.tanh(v / 44)) for v in bank['bass']]
    voices = [('Iron kick', kick, 54, False), ('Rumble bass', bass, 30, True),
              ('Acid stab', leads[3][::4], 24, True),
              ('Closed hat', bank['brush'], 21, False),
              ('Steel snare', bank['snare'], 35, False)]
    samples = [dict(name=n, pcm=p, volume=v, loop_start=0,
                    loop_length=len(p) if loop else 0) for n, p, v, loop in voices]
    samples += [dict(name='', pcm=[], volume=0, loop_start=0, loop_length=0)
                for _ in range(31-len(samples))]
    patterns = []
    riffs = [(428, 428, 404, 428, 320, 428, 360, 404),
             (428, 320, 428, 360, 404, 428, 285, 320)]
    for phrase in range(5):
        rows = []
        for row in range(64):
            cells = [dict(sample=0, period=0, effect=0, param=0) for _ in range(4)]
            breakdown = phrase == 4 and row < 32
            if row % 4 == 0 and not breakdown:
                cells[0].update(sample=1, period=214)
            if row % 4 in (1, 2, 3) and not breakdown:
                cells[1].update(sample=2, period=856 if row % 16 < 12 else 808,
                                effect=12, param=32 if row % 4 == 2 else 19)
            elif row % 4 == 0:
                cells[1].update(effect=12, param=0)
            if row % 8 in (0, 3, 6):
                note = riffs[phrase % 2][row // 8]
                cells[2].update(sample=3, period=note,
                                effect=12, param=16 if breakdown else 26)
            else:
                cells[2].update(effect=12, param=4)
            if row % 8 == 4 or (phrase == 4 and row >= 56 and row % 2 == 0):
                cells[3].update(sample=5, period=214)
            elif row % 2 == 0:
                cells[3].update(sample=4, period=143,
                                effect=12, param=24 if row % 4 == 2 else 12)
            if phrase == row == 0:
                cells[0].update(effect=15, param=160)
                cells[3].update(effect=15, param=6)
            rows.append(cells)
        patterns.append(rows)
    return music_mod.encode(dict(title='Iron Pulse', restart=0, orders=list(range(5)),
                                 samples=samples, patterns=patterns))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(compose())
