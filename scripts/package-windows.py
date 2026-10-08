#!/usr/bin/env python3
"""Package the Windows x64 release with SDL2 and verify an extracted copy."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile
from package_art import stage_art, verify_art

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--dll', type=Path, required=True)
parser.add_argument('--license', type=Path, required=True)
parser.add_argument('--output', type=Path, default=root / 'build/windows-release')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
version = (root / 'VERSION').read_text().strip()
archive = args.output / f'jelligotchi-{version}-windows-x64.zip'
with tempfile.TemporaryDirectory(prefix='jelli-windows-') as temporary:
    folder = Path(temporary)
    shutil.copy2(args.binary, folder / 'jelligotchi.exe')
    shutil.copy2(args.dll, folder / 'SDL2.dll')
    shutil.copy2(args.license, folder / 'SDL-LICENSE.txt')
    (folder / 'README.txt').write_text(
        f'Jelligotchi {version} — Windows x64\n'
        'Extract the entire ZIP, then open jelligotchi.exe. Keep SDL2.dll beside it.\n'
        'Click to play; Escape quits. The default session is unsaved.\n'
        'For saves: jelligotchi.exe --save "%LOCALAPPDATA%\\jelligotchi-pet"\n'
        'This build is unsigned. Live debug sockets are not supported on Windows yet.\n', encoding='utf-8')
    stage_art(folder)
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as output:
        for path in sorted(folder.rglob('*')):
            if path.is_file():
                output.write(path, path.relative_to(folder))
    verify_art(archive)
    extracted = folder / 'extracted'
    with zipfile.ZipFile(archive) as output:
        output.extractall(extracted)
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    subprocess.run([str(extracted / 'jelligotchi.exe'), '--headless', '--frames', '64'],
                   cwd=extracted, env=env, check=True)
print(archive)
