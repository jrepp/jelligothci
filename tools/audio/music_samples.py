"""Regenerate the original compact Morning Tea sample bank; no external recordings."""
import argparse
import math
import random
from pathlib import Path


def generate():
    rng = random.Random(731)
    voices = {
        'keys': [round(72*math.sin(2*math.pi*i/128)+25*math.sin(4*math.pi*i/128)
                       +12*math.sin(6*math.pi*i/128)) for i in range(128)],
        'bass': [round(82*math.sin(2*math.pi*i/64)+15*math.sin(4*math.pi*i/64))
                 for i in range(64)],
        'brush': [round(rng.randint(-70, 70)*(1-i/256)) for i in range(256)],
    }
    source = ('#include "music_samples.h"\n'
              '/* Original generated PCM: mellow keys, warm bass, brushed percussion.\n'
              ' * Reproduce with tools/audio/music_samples.py; never a streamed whole song. */\n')
    for name, values in voices.items():
        source += f'const int8_t jelli_music_{name}[{len(values)}] = {{\n'
        source += '\n'.join('    '+', '.join(map(str, values[i:i+16]))+','
                            for i in range(0, len(values), 16))+'\n};\n'
    return source


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_text(generate())
