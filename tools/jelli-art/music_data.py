"""Studio's bounded MOD documents, PCM sample import and four-channel audition."""
import base64
import binascii
import io
from pathlib import Path
import sys
import wave

import music_mod
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'audio'))
import build_music
from music_samples import tracker_library

CATALOG = [
    ('morning-tea', 'Morning Tea', 'Chill'), ('garden-waltz', 'Garden Waltz', 'Classical'),
    ('porch-picnic', 'Porch Picnic', 'Country'), ('tiny-thunder', 'Tiny Thunder', 'Heavy metal'),
    ('neon-heart', 'Neon Heart', 'K-pop'), ('cloud-club', 'Cloud Club', 'House'),
    ('pixel-sunset', 'Pixel Sunset', '80s'),
    ('pocket-arcade', 'Pocket Arcade', 'Game launcher'),
    ('garden-drift', 'Garden Drift', 'Garden / fishing'),
    ('tea-steam', 'Tea Steam', 'Tea / meals'),
    ('bubble-bath', 'Bubble Bath', 'Wash / care'),
    ('moon-pillow', 'Moon Pillow', 'Sleep / quiet time'),
    ('little-steps', 'Little Steps', 'Play / exercise'),
    ('bubble-rush', 'Bubble Rush', 'Bubble Lab / chiptune'),
    ('iron-pulse', 'Iron Pulse', 'Workout / hard techno')]


def key(value):
    if value not in [s[0] for s in CATALOG]:
        raise ValueError('Unknown record')
    return value


def initial():
    return {'title': 'New score', 'restart': 0, 'orders': [0],
            'samples': [{'name': '', 'volume': 64, 'loop_start': 0, 'loop_length': 0, 'pcm': []} for _ in range(31)],
            'patterns': [[[{'sample': 0, 'period': 0, 'effect': 0, 'param': 0} for _ in range(4)] for _ in range(64)]]}


def sample_presets():
    return tracker_library()


def binary(value, limit):
    if not isinstance(value, str) or len(value) > (limit+2)//3*4:
        raise ValueError('Encoded file exceeds the import limit')
    try:
        data = base64.b64decode(value, validate=True)
    except (ValueError, binascii.Error) as exc:
        raise ValueError('Invalid base64 file') from exc
    if len(data) > limit:
        raise ValueError('File exceeds the import limit')
    return data


def import_module(value):
    return {'data': music_mod.decode(binary(value, music_mod.MAX_BYTES))}


def export_module(doc):
    return {'file': base64.b64encode(music_mod.encode(doc)).decode('ascii')}


def import_sample(value, kind):
    data = binary(value, 65536)
    if kind == 'wav':
        try:
            with wave.open(io.BytesIO(data), 'rb') as wav:
                if wav.getnchannels() != 1 or wav.getsampwidth() != 1 or wav.getcomptype() != 'NONE':
                    raise ValueError('Use uncompressed 8-bit mono WAV')
                rate = wav.getframerate()
                if wav.getnframes() > 8192:
                    raise ValueError('Samples are limited to 8192 bytes')
                frames = wav.getnframes()
                raw = wav.readframes(frames)
                if len(raw) != frames:
                    raise ValueError('Truncated PCM WAV')
                pcm = [n-128 for n in raw]
        except (wave.Error, EOFError) as exc:
            raise ValueError('Invalid PCM WAV') from exc
    elif kind == 'pcm':
        rate = None
        if len(data) > 8192:
            raise ValueError('Samples are limited to 8192 bytes')
        pcm = [n if n < 128 else n-256 for n in data]
    else:
        raise ValueError('Choose WAV or signed 8-bit PCM')
    if not pcm:
        raise ValueError('Sample contains no audio')
    if len(pcm) % 2:
        pcm.append(0)
    return {'pcm': pcm, 'rate': rate}


def wav_bytes(pcm):
    output = io.BytesIO()
    with wave.open(output, 'wb') as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(22050)
        wav.writeframes(pcm)
    return base64.b64encode(output.getvalue()).decode('ascii')


def preview(doc):
    music_mod.encode(doc)  # Reject unsupported effects before compiling or playing anything.
    return {'stems': [wav_bytes(pcm) for pcm in build_music.render_stems(doc)], 'seconds': 30}
