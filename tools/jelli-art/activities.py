"""Validate authored activities through the engine's CMake generator."""
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from behaviors import BehaviorError, cmake_messages

ANIMATIONS = ('hold', 'sip', 'watch', 'breathe', 'jog', 'cast', 'dream', 'rest', 'think', 'lift',
              'sketch', 'dig', 'kick', 'volley', 'swim', 'swing', 'catch', 'mix')


def validate(doc, repo, content, manifest):
    if not isinstance(doc, dict) or not isinstance(doc.get('moments'), list):
        raise BehaviorError('Activities need a moments list')
    old = json.loads((Path(content) / 'activities.json').read_text())['moments']
    if len(doc['moments']) < len(old):
        raise BehaviorError('Keep existing activity IDs; disable an activity through its eligibility instead')
    # IDs are serialized. Reordering old entries must not silently reinterpret saves.
    for before, after in zip(old, doc['moments']):
        if before['id'] != after.get('id') or before['name'] != after.get('name'):
            raise BehaviorError('Existing activity IDs and names are stable; append new activities')
    assets = {a['id'] for a in manifest['assets']}
    forms = {f['id'] for f in json.loads((Path(content) / 'pets.json').read_text())['forms']}
    names = set()
    for m in doc['moments']:
        if m.get('name') in names:
            raise BehaviorError('Activity names must be unique')
        names.add(m.get('name'))
        if m.get('icon') not in assets or (m.get('prop') and m['prop'] not in assets):
            raise BehaviorError('Choose existing icon and prop sprites')
        if not isinstance(m.get('forms'), list) or any(f not in forms for f in m['forms']):
            raise BehaviorError('Choose existing pet forms')
    with tempfile.TemporaryDirectory(prefix='jelli-activities-') as scratch:
        work = Path(scratch)
        (work / 'cmake').mkdir()
        (work / 'content').mkdir()
        for name in ('JelliActivities.cmake', 'JelliLocations.cmake', 'JelliActivityCosts.cmake'):
            shutil.copy(Path(repo) / 'cmake' / name, work / 'cmake')
        shutil.copy(Path(content) / 'locations.json', work / 'content')
        (work / 'assets/slice').mkdir(parents=True)
        (work / 'assets/slice/assets.json').write_text(json.dumps(manifest))
        shutil.copy(Path(content) / 'pets.json', work / 'content')
        (work / 'content/activities.json').write_text(json.dumps(doc))
        driver = work / 'check.cmake'
        driver.write_text('cmake_minimum_required(VERSION 3.21)\n'
                          'include("${CMAKE_CURRENT_LIST_DIR}/cmake/JelliActivities.cmake")\n'
                          'jelli_activities_data(output)\n')
        result = subprocess.run(['cmake', '-P', str(driver)], cwd=work,
                                capture_output=True, text=True, timeout=20, check=False)
    if result.returncode:
        raise BehaviorError(cmake_messages(result.stderr))
