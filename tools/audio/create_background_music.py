"""Author original, quiet four-channel MOD scores for activities and the launcher.

Explicit regeneration only; normal builds preserve tracker edits. Uses the
existing synthesized PCM bank, original motifs, and no external recordings.
"""
import argparse
from pathlib import Path
import sys
from music_samples import sample_bank

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'jelli-art'))
import music_mod

# Sixteen melody notes over two patterns. Zero leaves a deliberate breath.
# PAL periods: C2=428, D2=381, E2=339, G2=285, A2=254, C3=214.
SCORES = (
    ('pocket-arcade', 'Pocket Arcade', 'Game launcher', 6, 0, 25,
     (428, 339, 285, 214, 254, 285, 339, 0, 381, 285, 254, 190, 214, 254, 285, 428),
     (856, 678, 570, 762)),
    ('garden-drift', 'Garden Drift', 'Garden / fishing', 12, 1, 20,
     (339, 285, 254, 0, 285, 214, 254, 339, 381, 339, 285, 0, 254, 285, 381, 428),
     (856, 640, 678, 762)),
    ('tea-steam', 'Tea Steam', 'Tea / meals', 12, 0, 18,
     (428, 339, 0, 285, 320, 254, 0, 214, 339, 285, 0, 254, 381, 339, 285, 0),
     (856, 640, 678, 570)),
    ('bubble-bath', 'Bubble Bath', 'Wash / care', 12, 4, 17,
     (285, 214, 0, 254, 339, 285, 0, 214, 254, 190, 0, 214, 285, 339, 381, 0),
     (856, 570, 640, 762)),
    ('moon-pillow', 'Moon Pillow', 'Sleep / quiet time', 12, 1, 14,
     (428, 0, 339, 0, 285, 0, 339, 381, 320, 0, 254, 0, 285, 339, 0, 428),
     (856, 678, 640, 856)),
    ('little-steps', 'Little Steps', 'Play / exercise', 12, 2, 22,
     (428, 285, 339, 254, 285, 214, 254, 0, 381, 254, 285, 214, 254, 285, 339, 428),
     (856, 570, 762, 678)),
)


def compose(score):
    key, title, _, speed, timbre, gain, melody, bass = score
    leads, bank = sample_bank()
    voices = [('Soft keys', leads[timbre][::4], gain, True),
              ('Round bass', bank['bass'], gain, True),
              ('Soft pulse', bank['kick'], max(6, gain // 2), False),
              ('Air', bank['brush'], 5 if key == 'moon-pillow' else 9, False)]
    samples = [dict(name=n, pcm=pcm, volume=v, loop_start=0,
                    loop_length=len(pcm) if loop else 0) for n, pcm, v, loop in voices]
    samples += [dict(name='', pcm=[], volume=0, loop_start=0, loop_length=0)
                for _ in range(31 - len(samples))]
    patterns = []
    for pattern in range(2):
        rows = []
        for row in range(64):
            cells = [dict(sample=0, period=0, effect=0, param=0) for _ in range(4)]
            note = melody[pattern * 8 + row // 8]
            if row % 8 == 0 and note:
                cells[0].update(sample=1, period=note)
            elif row % 8 == 3:
                cells[0].update(effect=12, param=gain // 3)
            elif row % 8 == 6:
                cells[0].update(effect=12, param=0)
            if row % 16 == 0:
                cells[1].update(sample=2, period=bass[(pattern * 4 + row // 16) % 4])
            if row % (32 if key == 'moon-pillow' else 8) == 0:
                cells[2].update(sample=3, period=214)
            if row % 16 == 8:
                cells[3].update(sample=4, period=285)
            if row == 0 and pattern == 0:
                cells[2].update(effect=15, param=128)
                cells[3].update(effect=15, param=speed)
            rows.append(cells)
        patterns.append(rows)
    return key, music_mod.encode(dict(title=title, restart=0, orders=[0, 1],
                                      samples=samples, patterns=patterns))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for score in SCORES:
        key, raw = compose(score)
        (args.output_dir / f'{key}.mod').write_bytes(raw)
