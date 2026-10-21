"""Exercise the real standalone record exporter and its WAV container."""
import array
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

with tempfile.TemporaryDirectory() as scratch:
    path = Path(scratch) / 'morning-tea.wav'
    subprocess.run([sys.argv[1], str(path)], check=True)
    with wave.open(str(path), 'rb') as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 22050)
        assert wav.getnframes() == 661500
        raw = wav.readframes(wav.getnframes())
    samples = array.array('h', raw)
    if sys.byteorder != 'little':
        samples.byteswap()
    peak = max(map(abs, samples))
    assert 1000 < peak < 32767
    assert any(samples[:22050]) and samples[-1] == 0
    assert sum(abs(v) for v in samples[-2205:]) < sum(abs(v) for v in samples[:2205])
    assert path.stat().st_size == len(raw) + 44
    print(f'Morning Tea WAV: 30 seconds, {peak} PCM peak, {path.stat().st_size} bytes')
