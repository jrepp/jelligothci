"""Shared editable-art payload and archive verification for native releases."""
import hashlib
import json
import re
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def stage_art(folder):
    preview = ROOT / 'build/assets/preview.html'
    if not preview.is_file():
        raise RuntimeError('Build the asset preview before packaging')
    shutil.copytree(ROOT / 'assets/slice', folder / 'assets/slice',
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '.DS_Store'))
    shutil.copytree(ROOT / 'tools/assets', folder / 'tools/assets',
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    (folder / 'scripts').mkdir(exist_ok=True)
    for name in ('jelli-art-live', 'uv'):
        shutil.copy2(ROOT / 'scripts' / name, folder / 'scripts' / name)
    shutil.copy2(ROOT / 'toolchain.env', folder / 'toolchain.env')
    shutil.copy2(preview, folder / 'preview.html')
    (folder / 'ARTWORK.txt').write_text(
        'Editable PNGs: assets/slice/ (79 images, including nine unique prizes).\n'
        'Open preview.html for the standalone art sheet.\n'
        'macOS/Linux live art: ./scripts/jelli-art-live\n'
        'Windows live art: uv run --python 3.12 tools/assets/live_assets.py\n'
        'Windows requires uv; macOS/Linux uses the bundled pinned uv wrapper.\n'
        'First art-tool run downloads Python and Pillow; playing the game needs neither.\n'
        'Save PNG edits at their original dimensions. Live reload validates binary alpha,\n'
        'recalculates bounds/centroids, and retains the last valid pack on errors.\n'
        'Embedded art is unchanged; start through the live-art tool to see edits.\n', encoding='utf-8')


def prize_count():
    """The engine's present count, so packaging follows include/jelli/prizes.h."""
    match = re.search(r'#define JELLI_PRIZE_COUNT (\d+)u', (ROOT / 'include/jelli/prizes.h').read_text())
    if not match:
        raise RuntimeError('JELLI_PRIZE_COUNT not found in include/jelli/prizes.h')
    return int(match.group(1))


def verify_art(archive):
    manifest = json.loads((ROOT / 'assets/slice/assets.json').read_text())
    with zipfile.ZipFile(archive) as package:
        for item in manifest['assets']:
            relative = 'assets/slice/' + item['path']
            expected = (ROOT / relative).read_bytes()
            if package.read(relative) != expected:
                raise RuntimeError(f'Packaged art differs: {relative}')
        count = prize_count()
        prizes = [item for item in manifest['assets'] if item['kind'] == 'prizes']
        if len(prizes) != count or len({hashlib.sha256(package.read('assets/slice/' + p['path'])).digest()
                                       for p in prizes}) != count:
            raise RuntimeError(f'Release must contain {count} distinct present PNGs (JELLI_PRIZE_COUNT)')
        for name in ('assets/slice/assets.json', 'preview.html', 'tools/assets/live_assets.py',
                     'scripts/jelli-art-live', 'ARTWORK.txt'):
            if not package.read(name):
                raise RuntimeError(f'Missing release art payload: {name}')
