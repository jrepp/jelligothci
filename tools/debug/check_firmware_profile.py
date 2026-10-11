"""Check build isolation after compiling each firmware profile (no device access)."""
import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', choices=('normal', 'power', 'factory'))
    parser.add_argument('build', type=Path)
    args = parser.parse_args()
    enabled = args.profile == 'power'
    factory = args.profile == 'factory'
    config = (args.build / 'sdkconfig').read_text()
    commands = json.loads((args.build / 'compile_commands.json').read_text())
    sources = {Path(command['file']).as_posix() for command in commands}
    probes = {name for name in sources if '/main/validation/' in name}
    assert bool(probes) == (enabled or factory), f'Unexpected experiment sources: {probes}'
    for flag in ('JELLI_POWER_EXPERIMENTS', 'PM_ENABLE', 'PM_PROFILING',
                 'FREERTOS_USE_TICKLESS_IDLE'):
        assert (f'CONFIG_{flag}=y' in config.splitlines()) == enabled, flag
    assert ('CONFIG_JELLI_FACTORY_FIRMWARE=y' in config.splitlines()) == factory
    if factory:
        assert any(name.endswith('/validation/factory/main.c') for name in probes)
        root = Path(__file__).resolve().parents[2]
        core = {name for name in sources if name.startswith((root / 'core').as_posix() + '/')}
        assert core == {(root / 'core/debug_protocol.c').as_posix()}
        main_sources = {name.split('/ports/esp32/main/', 1)[1] for name in sources
                        if '/ports/esp32/main/' in name}
        assert main_sources == {'validation/factory/main.c', 'validation/factory/commands.c',
                                'validation/factory/suite.c', 'validation/boards/pmic_probe.c',
                                'validation/boards/inventory.c', 'debug_usb.c'}
    assert any(name.endswith('/drivers/qmi8658.c') for name in sources) == enabled, 'Active IMU driver leaked across profiles'
    assert any(name.endswith('/drivers/pcf85063.c') for name in sources) == (not factory), 'RTC session driver isolation'
    generated = [s for s in sources if s.endswith('/co5300_sleep_trial.c')]
    assert bool(generated) == enabled, 'Panel policy seam leaked across profiles'
    if enabled:
        for source in ('power/power_diag.c', 'power/touch_trial.c', 'power/panel_trial.c', 'power/imu_trial.c', 'boards/qmi_probe.c', 'boards/waveshare_31261.c', 'network_probe.c'):
            assert any(name.endswith('/validation/' + source) for name in probes), source
    print(f'{args.profile}: firmware profile isolation verified')


if __name__ == '__main__':
    main()
