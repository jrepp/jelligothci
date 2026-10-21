"""Compare all native MOD exports against studio's exact preview renderer."""
import array
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(root / 'tools/audio'), str(root / 'tools/jelli-art')]
from build_music import KEYS, render_pcm
from music_mod import decode

with tempfile.TemporaryDirectory() as scratch:
    for song, key in enumerate(KEYS):
        path = Path(scratch) / (key + '.wav')
        subprocess.run([sys.argv[1], str(path), str(song)], check=True)
        with wave.open(str(path), 'rb') as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 22050)
            assert wav.getnframes() == 661500
            raw = wav.readframes(wav.getnframes())
        expected = array.array('h', render_pcm(decode((root / 'content/music' / (key + '.mod')).read_bytes())))
        actual = array.array('h', raw)
        if sys.byteorder != 'little':
            expected.byteswap()
            actual.byteswap()
        expected = array.array('h', (int(value * 80 / 100) for value in expected))
        assert actual == expected, key + ': native and preview disagree'
        peak = max(map(abs, actual))
        assert 1000 < peak < 32767
        assert path.stat().st_size == len(raw) + 44
        print(f'{key}: exact native/preview match, 30 seconds, peak {peak}')
