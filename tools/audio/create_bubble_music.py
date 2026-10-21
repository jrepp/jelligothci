"""Compose Bubble Rush: an original four-channel, 30-second arcade MOD loop.

Explicit regeneration preserves subsequent Studio tracker edits. Four phrases
at 128 BPM fill the runtime's 30-second loop exactly; no imported recordings.
"""
from pathlib import Path
import sys
from music_samples import sample_bank

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'jelli-art'))
import music_mod


def compose():
    leads, bank = sample_bank()
    voices = [('Arcade lead', leads[3][::4], 37, True),
              ('Spark arps', leads[6][::4], 19, True),
              ('Sine sub bass', bank['sub_bass'], 31, True),
              ('Tight drive kick', bank['tight_kick'], 48, False),
              ('Snare', bank['snare'], 39, False),
              ('Metal hat', bank['metal_hat'], 15, False)]
    samples = [dict(name=n, pcm=p, volume=v, loop_start=0,
                    loop_length=len(p) if loop else 0) for n, p, v, loop in voices]
    samples += [dict(name='', pcm=[], volume=0, loop_start=0, loop_length=0)
                for _ in range(31-len(samples))]
    # C minor / Ab / Eb / Bb, with an original syncopated call and response.
    chords = [(428, 360, 285), (538, 428, 360), (360, 285, 240), (480, 381, 320)]
    hooks = [
        [214, 0, 180, 214, 143, 0, 180, 160, 180, 214, 240, 214, 180, 0, 143, 160],
        [180, 0, 143, 120, 143, 160, 180, 0, 160, 190, 160, 143, 160, 0, 190, 240],
        [107, 0, 143, 180, 143, 107, 120, 143, 180, 160, 143, 120, 143, 0, 180, 160],
        [180, 143, 120, 107, 120, 143, 180, 0, 160, 190, 240, 190, 160, 143, 190, 214],
    ]
    patterns = []
    for phrase in range(4):
        rows = []
        for row in range(64):
            cells = [dict(sample=0, period=0, effect=0, param=0) for _ in range(4)]
            chord = chords[(phrase * 2 + row // 32) % 4]
            note = hooks[phrase][row // 4]
            if row % 4 == 0 and note:
                cells[0].update(sample=1, period=note)
            elif row % 4 == 2:
                cells[0].update(effect=12, param=18)
            elif row % 4 == 3:
                cells[0].update(effect=12, param=0)
            # Answer the existing hook's rests, then thicken the later phrases.
            if phrase >= 2 or row % 2 == 0 or not note:
                cells[1].update(sample=2, period=chord[(row + phrase) % 3] // 2)
            if row % 4 == 0:
                cells[2].update(sample=3, period=chord[0] * (2 if row % 16 < 8 else 1))
            elif row % 16 == 14:
                cells[2].update(sample=3, period=chord[2] * 2, effect=12, param=25)
            elif row % 4 == 3:
                cells[2].update(effect=12, param=0)
            drum = 4 if row % 16 in (0, 10) else 5 if row % 16 == 8 else 6
            if row % 2 == 0 or (phrase == 3 and row >= 60):
                cells[3].update(sample=drum, period=160 if drum == 6 else 214)
            if phrase == row == 0:
                cells[2].update(effect=15, param=128)
                cells[3].update(effect=15, param=6)
            for cell in cells:
                if cell['period']:
                    cell['period'] = min(music_mod.PERIODS, key=lambda p: abs(p-cell['period']))
            rows.append(cells)
        patterns.append(rows)
    return music_mod.encode(dict(title='Bubble Rush', restart=0, orders=[0, 1, 2, 3],
                                 samples=samples, patterns=patterns))


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(compose())
