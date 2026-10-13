"""Verify the isolated cache build matrix and write artifact identities; no device access."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


VARIANTS = {
    'baseline': (16, 32, 32),
    'icache': (32, 32, 32),
    'dcache': (16, 64, 32),
    'line64': (16, 32, 64),
    'icache-line64': (32, 32, 64),
    'combined': (32, 64, 64),
}
FAMILIES = (
    ('INSTRUCTION_CACHE', (16, 32), 'KB', 1024),
    ('DATA_CACHE', (16, 32, 64), 'KB', 1024),
    ('DATA_CACHE_LINE', (16, 32, 64), 'B', 1),
)


def read_config(text):
    result = {}
    for line in text.splitlines():
        if line.startswith('CONFIG_'):
            key, value = line.split('=', 1)
            result[key] = value
        elif line.startswith('# CONFIG_') and line.endswith(' is not set'):
            result[line[2:-11]] = 'n'
    return result


def expected_cache(variant):
    result = {}
    for (family, choices, unit, multiplier), selected in zip(FAMILIES, VARIANTS[variant]):
        prefix = 'CONFIG_ESP32S3_' + family
        for size in choices:
            result[f'{prefix}_{size}{unit}'] = 'y' if size == selected else 'n'
        result[prefix + '_SIZE'] = str(selected * multiplier)
    return result


def check_settings(config, variant):
    for key, expected in expected_cache(variant).items():
        actual = config.get(key, 'n')
        if key.endswith('_SIZE') and actual is not None:
            actual = str(int(actual, 0))
        if actual != expected:
            raise ValueError(f'{variant}: {key}: expected {expected}, got {actual}')
    for key, expected in (('CONFIG_JELLI_POWER_EXPERIMENTS', 'y'),
                          ('CONFIG_JELLI_MEMORY_TRIAL', '0')):
        if config.get(key) != expected:
            raise ValueError(f'{variant}: wrong experiment setting {key}')


def compare(reference, candidate, allowed=()):
    drift = {key for key in reference.keys() | candidate.keys()
             if reference.get(key, 'n') != candidate.get(key, 'n')} - set(allowed)
    if drift:
        raise ValueError('Unrelated configuration drift: ' + ', '.join(sorted(drift)))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=Path, default=root / 'build')
    parser.add_argument('--reference', type=Path, default=root / 'build/esp32-power/sdkconfig')
    parser.add_argument('--output', type=Path, default=root / 'build/cache-trial-manifest.json')
    args = parser.parse_args()
    # Never leave an earlier acceptance manifest after a failed recheck.
    args.output.unlink(missing_ok=True)
    baseline = read_config((args.build_root / 'esp32-cache-baseline/sdkconfig').read_text())
    check_settings(baseline, 'baseline')
    compare(read_config(args.reference.read_text()), baseline)
    artifacts = {}
    for variant in VARIANTS:
        directory = args.build_root / ('esp32-cache-' + variant)
        config = read_config((directory / 'sdkconfig').read_text())
        check_settings(config, variant)
        compare(baseline, config, expected_cache(variant))
        # Check the generated build configuration as well as the editable input.
        generated = json.loads((directory / 'config/sdkconfig.json').read_text())
        for key, value in config.items():
            name = key.removeprefix('CONFIG_')
            if name not in generated:
                continue  # IDF compatibility aliases need not be generated.
            actual = generated[name]
            if isinstance(actual, bool):
                expected = value == 'y'
            elif isinstance(actual, int):
                expected = int(value, 0)
            else:
                expected = value.strip('"')
            if actual != expected:
                raise ValueError(f'{variant}: stale generated configuration: {key}')
        subprocess.run([sys.executable, str(root / 'tools/debug/check_firmware_profile.py'),
                        'power', str(directory)], check=True)
        artifacts[variant] = {
            'cache': dict(zip(('instruction_kib', 'data_kib', 'data_line_bytes'), VARIANTS[variant])),
            'extra_cache_bytes': (VARIANTS[variant][0] - 16 + VARIANTS[variant][1] - 32) * 1024,
            'sha256': {name: digest(directory / name) for name in
                       ('sdkconfig', 'jelligotchi.bin', 'jelligotchi.elf', 'bootloader/bootloader.bin')},
        }
    report = {'artifacts': artifacts, 'reference_sha256': digest(args.reference),
              'device_tested': False, 'physical_verified': False}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(f'Cache matrix verified: {args.output}')


if __name__ == '__main__':
    main()
