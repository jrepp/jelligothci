"""Export the exact portable synthesizer to mono WAVs and a standalone audition sheet."""
import argparse
import base64
from pathlib import Path
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[2]
CUES = [('chirp', 'Two rising digital bubbles'), ('happy', 'A bright four-note greeting'),
        ('sparkle', 'A tiny ascending celebration'), ('hello', 'Oo–ah–ee pet babble'),
        ('sleepy', 'A soft descending vowel warble'), ('tap', 'A quiet 65 ms rising menu blip'), ('coo', 'A soft rounded idle coo')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/desktop/jelli_sound_export')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/audio')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cards = []
    for index, (name, description) in enumerate(CUES):
        path = args.output / f'{name}.wav'
        subprocess.run([str(args.binary), str(index), str(path)], check=True)
        with wave.open(str(path), 'rb') as wav:
            assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) == (1, 2, 22050)
            duration = wav.getnframes() / wav.getframerate()
        data = base64.b64encode(path.read_bytes()).decode()
        cards.append(f'<article><h2>{name.title()}</h2><p>{description}</p>'
                     f'<audio controls preload="none" src="data:audio/wav;base64,{data}"></audio>'
                     f'<small>{duration:.3f}s · mono PCM16 · 22,050 Hz · {path.stat().st_size:,} bytes exported</small></article>')
    html = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Jelligotchi · Little digital voices</title><style>
body{max-width:850px;margin:50px auto;padding:0 24px;background:#f5f1e7;color:#263c37;font:16px/1.6 system-ui}h1{font-size:42px;line-height:1.1}section{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px}article{padding:24px;border:1px solid #d8ded2;border-radius:16px;background:#fffcf4}h2{margin:0}small{display:block;margin-top:12px;color:#63716b}audio{width:100%}</style>
<h1>Little digital voices.</h1><p>Seven original procedural cues, rendered by the same C synthesizer used on the pet. Hello and Sleepy are vowel-like babble, not text-to-speech. Start with a comfortable listening volume.</p>
<p>The device stores tiny note/formant programs, not these WAV files. No recording, external voice model, or network service is used.</p><section>'''+''.join(cards)+'</section></html>'
    (args.output / 'preview.html').write_text(html)
    print(f'Exported seven cues: {args.output / "preview.html"}')


if __name__ == '__main__':
    main()
