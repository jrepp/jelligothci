"""Recreate original starter MODs from synthesized PCM and fixed original motifs.

Explicit authoring utility, never a build step: normal builds must preserve
canonical MOD edits. Generate into a scratch --output-dir and compare before
intentionally replacing a starter. No recordings or third-party melodies used.
"""
import argparse
from pathlib import Path
import sys
import music_samples
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'jelli-art'))
import music_mod

# key, title, tempo, lead timbre, kick volume, 32 lead periods (four motifs),
# four bass periods, 16-row kick/snare instruments, 16-row brush instruments.
# Periods are the standard ProTracker C1..B3 table; zero means no trigger.
SONGS = (('morning-tea', 'Morning Tea', 120, 0, 22,
  (428, 339, 285, 214, 285, 339, 381, 339, 254, 285, 339, 428, 339, 285, 254, 214, 320, 254, 214,
   170, 214, 254, 285, 339, 285, 226, 214, 226, 285, 339, 381, 428),
  (856, 508, 640, 570), (3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
  (5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 0, 5, 0)),
 ('garden-waltz', 'Garden Waltz', 96, 1, 22,
  (428, 285, 214, 285, 339, 285, 214, 170, 254, 214, 170, 214, 254, 285, 339, 428, 320, 254, 214,
   254, 320, 339, 428, 339, 285, 214, 226, 285, 339, 381, 339, 428),
  (856, 508, 640, 570), (3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
  (0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0)),
 ('porch-picnic', 'Porch Picnic', 112, 2, 54,
  (428, 285, 214, 285, 339, 428, 339, 285, 254, 254, 214, 254, 285, 339, 428, 0, 320, 254, 214, 254,
   214, 254, 320, 339, 285, 285, 226, 214, 226, 285, 339, 428),
  (856, 508, 640, 570), (3, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0),
  (0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0)),
 ('tiny-thunder', 'Tiny Thunder', 152, 3, 54,
  (428, 428, 214, 428, 428, 226, 254, 285, 254, 254, 170, 254, 254, 214, 226, 285, 320, 320, 214,
   320, 254, 320, 339, 428, 285, 285, 226, 285, 214, 226, 339, 428),
  (856, 508, 640, 570), (3, 0, 3, 0, 4, 0, 3, 0, 3, 0, 3, 0, 4, 0, 3, 0),
  (0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0)),
 ('neon-heart', 'Neon Heart', 128, 4, 54,
  (214, 0, 214, 170, 285, 214, 0, 285, 254, 214, 170, 0, 214, 254, 285, 0, 214, 254, 320, 254, 214,
   0, 170, 214, 226, 214, 0, 226, 285, 339, 381, 339),
  (856, 508, 640, 570), (3, 0, 0, 0, 4, 0, 0, 0, 3, 0, 3, 0, 4, 0, 0, 0),
  (0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0)),
 ('cloud-club', 'Cloud Club', 124, 5, 54,
  (428, 0, 285, 214, 0, 285, 214, 0, 254, 0, 214, 170, 0, 214, 254, 0, 320, 0, 254, 214, 0, 254,
   214, 0, 285, 0, 226, 214, 0, 226, 285, 0),
  (856, 508, 640, 570), (3, 0, 0, 0, 3, 0, 0, 0, 3, 0, 0, 0, 3, 0, 4, 0),
  (0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0)),
 ('pixel-sunset', 'Pixel Sunset', 108, 6, 54,
  (428, 214, 285, 214, 339, 214, 285, 170, 254, 170, 214, 170, 285, 214, 254, 214, 320, 214, 254,
   214, 320, 254, 214, 170, 285, 214, 226, 214, 339, 226, 285, 339),
  (856, 508, 640, 570), (3, 0, 0, 0, 4, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0),
  (0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0, 5, 0)))


def classical_row(row, index, pattern, melody):
    """Original phrase contrast over broken chords; preserve the existing pitch vocabulary."""
    row[0] = dict(sample=0, period=0, effect=0, param=0)
    spacing = 4 if pattern == 0 else 2
    if index % spacing == 0:
        row[0].update(sample=1, period=melody[index // 2])
    if index % 16 == 14:
        row[0] = dict(sample=0, period=0, effect=12, param=0)
    chords = ((856, 678, 570), (508, 428, 339), (640, 508, 428), (570, 453, 381))
    chord = chords[index // 16]
    if index % 2 == 0:
        row[1].update(sample=2, period=chord[(0, 2, 1, 2)[index // 2 % 4]],
                      effect=12, param=25)
    # A quiet sustained upper chord tone balances the active bass figure.
    row[2] = dict(sample=0, period=0, effect=0, param=0)
    if index % 16 == 0:
        row[2].update(sample=1, period=chord[0] // 2, effect=12, param=13)
    elif index % 16 == 12:
        row[2].update(effect=12, param=0)
    if row[3]['sample']:
        row[3].update(effect=12, param=8)


def compose(song):
    key, title, tempo, lead, kick_volume, melody, bass, drums, brush = song
    leads, bank = music_samples.sample_bank()
    instruments = (
        ('Lead', leads[lead][::4], 38, True),
        ('Bass', bank['bass'], 42, True),
        ('Kick', bank['kick'], kick_volume, False),
        ('Snare', bank['snare'], 44, False),
        ('Brush', bank['brush'], 28, False),
    )
    # 32-point lead at C2 period sounds approximately C4; the 64-point bass
    # at C1 period sounds approximately C2. Samples use deterministic synthesis.
    samples = [{'name': name, 'volume': volume, 'loop_start': 0,
                'loop_length': len(pcm) if loop else 0, 'pcm': pcm}
               for name, pcm, volume, loop in instruments]
    samples.extend({'name': '', 'volume': 0, 'loop_start': 0,
                    'loop_length': 0, 'pcm': []} for _ in range(31-len(samples)))
    patterns = []
    for pattern_index in range(2):
        rows = []
        for index in range(64):
            row = [{'sample': 0, 'period': 0, 'effect': 0, 'param': 0} for _ in range(4)]
            if index % 2 == 0 and melody[index//2]:
                row[0].update(sample=1, period=melody[index//2])
            if index % 4 == 0 and bass[index//16]:
                row[1].update(sample=2, period=bass[index//16])
            if drums[index % 16]:
                row[2].update(sample=drums[index % 16], period=160)
            if brush[index % 16]:
                row[3].update(sample=brush[index % 16], period=160)
            if key == 'garden-waltz':
                classical_row(row, index, pattern_index, melody)
            if index == 0 and pattern_index == 0:
                row[2].update(effect=15, param=tempo)
                row[3].update(effect=15, param=6)
            rows.append(row)
        patterns.append(rows)
    return key, music_mod.encode({'title': title, 'restart': 0, 'orders': [0, 1],
                                  'samples': samples, 'patterns': patterns})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path, required=True,
                        help='Explicit destination for generated starter MODs')
    parser.add_argument('--song', choices=[song[0] for song in SONGS],
                        help='Regenerate only the selected original starter')
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for song in SONGS:
        if args.song and song[0] != args.song:
            continue
        key, data = compose(song)
        (args.output_dir / f'{key}.mod').write_bytes(data)


if __name__ == '__main__':
    main()
