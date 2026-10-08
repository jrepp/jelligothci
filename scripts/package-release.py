#!/usr/bin/env python3
"""Package the macOS game, editable artwork, and self-contained preview."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def archive(folder, destination):
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as output:
        for path in sorted(folder.rglob('*')):
            if path.is_file():
                output.write(path, path.relative_to(folder))


def native(binary, stage):
    app = stage / 'Jelligotchi.app'
    mac = app / 'Contents/MacOS'
    mac.mkdir(parents=True)
    target = mac / 'jelligotchi'
    shutil.copy2(binary, target)
    queue = [target]
    copied = {}
    licenses = stage / 'licenses'
    licenses.mkdir()
    while queue:
        executable = queue.pop()
        lines = run('otool', '-L', str(executable)).splitlines()[1:]
        for line in lines:
            dependency = line.strip().split(' (')[0]
            if dependency.startswith(('/System/', '/usr/lib/', '@loader_path/')):
                continue
            source = Path(dependency)
            if not source.is_absolute() or not source.is_file():
                raise RuntimeError(f'Unresolved dependency: {dependency}')
            name = source.name
            resolved = source.resolve()
            if name in copied and copied[name] != resolved:
                raise RuntimeError(f'Conflicting library name: {name}')
            if name not in copied:
                copied[name] = resolved
                dest = mac / name
                shutil.copy2(resolved, dest)
                run('install_name_tool', '-id', '@loader_path/' + name, str(dest))
                queue.append(dest)
                prefix = resolved.parent.parent
                for notice in (prefix / 'share/licenses').rglob('*'):
                    if notice.is_file():
                        shutil.copy2(notice, licenses / (prefix.parent.name + '-' + notice.name))
                # SDL2-compat loads SDL3 at runtime, so otool cannot discover it.
                if 'sdl2-compat' in str(resolved):
                    sdl3 = Path(run('brew', '--prefix', 'sdl3')) / 'lib/libSDL3.dylib'
                    shutil.copy2(sdl3.resolve(), mac / 'libSDL3.dylib')
                    run('install_name_tool', '-id', '@loader_path/libSDL3.dylib', str(mac / 'libSDL3.dylib'))
                    queue.append(mac / 'libSDL3.dylib')
                    for notice in (sdl3.resolve().parent.parent / 'share/licenses').rglob('*'):
                        if notice.is_file():
                            shutil.copy2(notice, licenses / ('SDL3-' + notice.name))
            run('install_name_tool', '-change', dependency, '@loader_path/' + name, str(executable))
    minimum = (11, 0)
    for executable in mac.iterdir():
        output = run('otool', '-l', str(executable))
        for value in re.findall(r'\bminos (\d+(?:\.\d+)+)', output):
            minimum = max(minimum, tuple(map(int, value.split('.'))))
        run('codesign', '--force', '--sign', '-', str(executable))
    version = (ROOT / 'VERSION').read_text().strip()
    with (app / 'Contents/Info.plist').open('wb') as output:
        plistlib.dump(dict(CFBundleName='Jelligotchi', CFBundleDisplayName='Jelligotchi',
                          CFBundleIdentifier='dev.jelligotchi.game', CFBundleExecutable='jelligotchi',
                          CFBundlePackageType='APPL', CFBundleVersion=version,
                          CFBundleShortVersionString=version,
                          LSMinimumSystemVersion='.'.join(map(str, minimum))), output)
    run('codesign', '--force', '--sign', '-', str(app))
    run('codesign', '--verify', '--deep', '--strict', str(app))
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
    subprocess.run([str(target), '--headless', '--frames', '64'], env=env, check=True)
    (stage / 'README.txt').write_text(
        f'Jelligotchi {version} — macOS {platform.machine()}\n'
        'Open Jelligotchi.app. SDL is bundled; Homebrew is not required.\n'
        'This build is ad-hoc signed, not Apple notarized. macOS may require approval in Privacy & Security.\n'
        'Click/tap to play. Escape quits. Opening the app starts an unsaved session.\n'
        'CLI: Jelligotchi.app/Contents/MacOS/jelligotchi --help\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/desktop/jelligotchi')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/release')
    args = parser.parse_args()
    if platform.system() != 'Darwin':
        parser.error('Native packaging currently supports macOS only')
    args.output.mkdir(parents=True, exist_ok=True)
    version = (ROOT / 'VERSION').read_text().strip()
    with tempfile.TemporaryDirectory(prefix='jelli-package-') as temporary:
        stage = Path(temporary)
        native(args.binary.resolve(), stage)
        game_zip = args.output / f'jelligotchi-{version}-macos-{platform.machine()}.zip'
        archive(stage, game_zip)
    art_zip = args.output / f'jelligotchi-{version}-source-art.zip'
    archive(ROOT / 'assets', art_zip)
    preview = ROOT / 'build/assets/preview.html'
    if not preview.is_file():
        raise RuntimeError('Build the asset preview first')
    preview_out = args.output / f'jelligotchi-{version}-preview.html'
    shutil.copy2(preview, preview_out)
    preview_zip = args.output / f'jelligotchi-{version}-preview.zip'
    with zipfile.ZipFile(preview_zip, 'w', zipfile.ZIP_DEFLATED) as output:
        output.write(preview, 'preview.html')
    artifacts = [game_zip, art_zip, preview_out, preview_zip]
    (args.output / 'SHA256SUMS').write_text(''.join(
        f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n' for path in artifacts))
    print(json.dumps({'artifacts': [str(p) for p in artifacts]}, indent=2))


if __name__ == '__main__':
    main()
