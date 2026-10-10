"""Build-time cross-reference checks, including core-only CMake generation."""
import copy
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
original = json.loads((root / 'content/activities.json').read_text())
with tempfile.TemporaryDirectory(prefix='jelli-activity-refs-') as scratch:
    work = Path(scratch)
    (work / 'cmake').mkdir()
    (work / 'content').mkdir()
    (work / 'assets/slice').mkdir(parents=True)
    for name in ('JelliActivities.cmake', 'JelliLocations.cmake', 'JelliActivityCosts.cmake'):
        shutil.copy(root / 'cmake' / name, work / 'cmake')
    for name in ('pets.json', 'locations.json'):
        shutil.copy(root / 'content' / name, work / 'content')
    shutil.copy(root / 'assets/slice/assets.json', work / 'assets/slice')
    driver = work / 'check.cmake'
    driver.write_text('cmake_minimum_required(VERSION 3.21)\n'
                      'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliActivities.cmake")\n'
                      'jelli_activities_data(output)\n')

    def check(data, valid, label):
        (work / 'content/activities.json').write_text(json.dumps(data))
        result = subprocess.run(['cmake', '-P', str(driver)], cwd=work,
                                capture_output=True, text=True, timeout=20, check=False)
        assert (result.returncode == 0) == valid, (label, result.stderr or 'accepted invalid data')

    check(original, True, 'valid catalog')
    mutations = [
        lambda d: d['moments'][4].update(locations=['moon']),
        lambda d: d['moments'][4].update(locations=[]),
        lambda d: d['moments'][4].update(locations=['home', 'home']),
        lambda d: d['moments'][4].update(randomize_location=1),
        lambda d: d['moments'][4].update(forms=[99]),
        lambda d: d['moments'][4].update(icon=99999),
        lambda d: d['moments'][4].update(prop=99999),
        lambda d: d['moments'][4].update(requires=4),
        lambda d: d['moments'][4].update(requires=31),
        lambda d: d['moments'][4].update(bonuses=[{'form': 99, 'percent': 20}]),
        lambda d: d['moments'][4].update(bonuses=[{'form': 0, 'percent': 101}]),
        lambda d: d['moments'][4].update(bonuses=[{'form': 0, 'percent': 10}] * 2),
        lambda d: d['moments'][4].update(gains={'Wrong123': 10}),
        lambda d: d['moments'][4].update(costs={'energy': -1}),
        lambda d: d['moments'][4].update(jitter_pct=26),
        lambda d: d['pet_costs'][0].update(form=99),
        lambda d: d['pet_costs'][0].update(form=1),
        lambda d: d['pet_costs'][0].update(hydration_pct=301),
        lambda d: d['favorites']['profiles'][0]['windows'][0].update(moment='UNKNOWN'),
    ]
    for i, mutate in enumerate(mutations):
        data = copy.deepcopy(original)
        mutate(data)
        check(data, False, f'invalid reference/tuning {i}')
    places = json.loads((work / 'content/locations.json').read_text())
    places['locations'][1]['id'] = 7
    (work / 'content/locations.json').write_text(json.dumps(places))
    check(original, False, 'renumbered saved location')
print('PASS: activity build checks reject broken references, invalid effects and unstable locations')
