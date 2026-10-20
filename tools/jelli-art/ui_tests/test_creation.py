"""New view startup and real copied-checkout creation, including refresh recovery."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import urllib.request

import pytest
from playwright.sync_api import expect

from conftest import JELLI_ART, REPO, free_port


@pytest.fixture
def creator(tmp_path, page):
    source, content = tmp_path / "slice", tmp_path / "content"
    shutil.copytree(REPO / "assets/slice", source)
    shutil.copytree(REPO / "content", content)
    shutil.copytree(source, tmp_path / "before")
    port = free_port()
    log = (tmp_path / "studio.log").open("wb")
    process = subprocess.Popen([sys.executable, str(JELLI_ART / "jelli_art.py"), "--no-open", "--port", str(port),
                                "--assets", str(source), "--content", str(content)],
                               stdout=log, stderr=subprocess.STDOUT, env=os.environ.copy())
    url = f"http://127.0.0.1:{port}"
    try:
        deadline = time.monotonic() + 30
        while True:
            if process.poll() is not None:
                raise AssertionError((tmp_path / "studio.log").read_text())
            try:
                urllib.request.urlopen(url + "/healthz", timeout=1).read()
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise AssertionError("Creation studio did not start")
                time.sleep(0.1)
        page.add_init_script("localStorage.setItem('jelli-shell:guide','false');"
                             "localStorage.setItem('jelli-review:artist','\"Test Artist\"');"
                             f"localStorage.setItem('jelli-review:before',{json.dumps(json.dumps(str(tmp_path / 'before')))});")
        errors = []
        page.on("pageerror", lambda error: errors.append(str(error)))
        page.goto(url)
        page.wait_for_function("() => typeof D !== 'undefined' && D.assets?.length > 0")
        expect(page.get_by_role("tab", name="New", exact=True)).to_be_visible()
        page.get_by_role("tab", name="New", exact=True).click()
        expect(page.locator("#creation-form")).to_be_visible()
        yield {"page": page, "source": source, "content": content, "errors": errors}
        assert errors == []
    finally:
        process.terminate()
        process.wait(timeout=10)
        log.close()


def icon_fields(page, key):
    form = page.locator("#creation-form")
    form.locator('[name="kind"]').select_option("icons")
    form.locator('[name="key"]').fill(key)
    form.locator('[name="name"]').fill("TEST ICON")
    return form


def test_new_view_starts_and_creates_saved_asset_in_paint(creator):
    page = creator["page"]
    form = icon_fields(page, "browser-icon")
    form.get_by_role("button", name="Create and paint").click()
    expect(page.get_by_role("tab", name="Paint", exact=True)).to_have_attribute("aria-selected", "true", timeout=30000)
    page.wait_for_function("() => state.key === 'icons.browser-icon' && state.mode === 'paint'")
    manifest = json.loads((creator["source"] / "assets.json").read_text())
    asset = next(a for a in manifest["assets"] if a["key"] == "icons.browser-icon")
    assert (creator["source"] / asset["path"]).is_file()
    page.reload()
    page.wait_for_function("() => typeof D !== 'undefined' && D.assets?.some(a=>a.key === 'icons.browser-icon')")
    expect(page.get_by_role("tab", name="New", exact=True)).to_be_visible()


def test_creation_saved_but_reload_failed_keeps_feedback_and_retry(creator):
    page = creator["page"]
    form = icon_fields(page, "refresh-recovery")
    page.route("**/api/data?**", lambda route: route.abort())
    form.get_by_role("button", name="Create and paint").click()
    expect(page.locator("#creation-message")).to_contain_text("Created and saved icons.refresh-recovery", timeout=30000)
    expect(form.get_by_role("button", name="Create and paint")).to_be_disabled()
    assert (creator["source"] / "icons/refresh-recovery.png").is_file()
    page.unroute("**/api/data?**")
    form.get_by_role("button", name="Reload creation sources").click()
    page.wait_for_function("() => state.key === 'icons.refresh-recovery' && state.mode === 'paint'")


def test_creation_stale_error_preserves_fields_and_recovers(creator):
    page = creator["page"]
    form = icon_fields(page, "stale-recovery")
    path = creator["source"] / "assets.json"
    path.write_text(path.read_text() + "\n")
    form.get_by_role("button", name="Create and paint").click()
    expect(page.locator("#creation-message")).to_contain_text("Creation sources changed", timeout=30000)
    expect(form.locator('[name="key"]')).to_have_value("stale-recovery")
    assert not (creator["source"] / "icons/stale-recovery.png").exists()
    form.get_by_role("button", name="Reload creation sources").click()
    expect(page.locator("#creation-message")).to_contain_text("Sources reloaded")
    form.get_by_role("button", name="Create and paint").click()
    page.wait_for_function("() => state.key === 'icons.stale-recovery' && state.mode === 'paint'", timeout=30000)


def test_new_pet_wizard_creates_distinct_form_and_catalog_entry(creator):
    page = creator["page"]
    form = page.locator("#creation-form")
    form.locator('[name="key"]').fill("browser-pet")
    form.locator('[name="name"]').fill("BROWSER PET")
    form.locator('[name="template_form"]').select_option("baby")
    form.locator('[name="size"]').select_option("48")
    form.locator('[name="gameplay_policy"]').select_option("copy-template")
    form.locator('[name="evolution"]').select_option("single-form")
    form.locator('[name="scale"]').fill("3")
    form.locator('[name="icon_scale"]').fill("1")
    form.locator('[name="portrait_scale"]').fill("3")
    form.locator('[name="unlock_prize"]').select_option("1")
    form.locator('[name="hint"]').fill("CATCH BUTTERFLY")
    form.get_by_role("button", name="Create and paint").click()
    page.wait_for_function("() => state.key?.startsWith('creatures.browser-pet-') && state.mode === 'paint'", timeout=30000)
    pets = json.loads((creator["content"] / "pets.json").read_text())
    assert pets["entries"][-1]["name"] == "BROWSER PET"
    assert pets["forms"][-1]["art"] == "browser-pet"
    assert pets["evolution_sets"][-1]["growth_ticks"] == 0


@pytest.mark.parametrize("kind", ["prizes", "backgrounds"])
def test_new_present_and_background_wizard_register_game_content(creator, kind):
    page = creator["page"]
    form = icon_fields(page, "browser-" + kind)
    form.locator('[name="kind"]').select_option(kind)
    if kind == "prizes":
        form.locator('[name="collectible"]').select_option("true")
    else:
        form.locator('[name="location_key"]').fill("browser_room")
        form.locator('[name="location_name"]').fill("BROWSER ROOM")
        form.locator('[name="outdoors"]').select_option("false")
    form.get_by_role("button", name="Create and paint").click()
    page.wait_for_function("key => state.key === key && state.mode === 'paint'", arg=kind + ".browser-" + kind, timeout=30000)
    path, field = ("prizes", "prizes") if kind == "prizes" else ("locations", "locations")
    doc = json.loads((creator["content"] / (path + ".json")).read_text())
    expected = "browser-prizes" if kind == "prizes" else "browser_room"
    assert doc[field][-1]["key"] == expected


def test_duplicate_creature_copies_saved_art_then_paints_independently(creator):
    from PIL import Image
    from test_layout import paint_something
    page = creator['page']
    before = json.loads((creator['source'] / 'assets.json').read_text())
    originals = [a for a in before['assets'] if a.get('form') == 'baby']
    source_bytes = {a['path']: (creator['source'] / a['path']).read_bytes() for a in originals}
    page.get_by_role('tab', name='Creature', exact=True).click()
    page.locator('#cr-forms').get_by_role('button', name='MINT · baby', exact=True).click()
    page.locator('#cr-duplicate').click()
    form = page.locator('#creation-form')
    expect(form).to_be_visible()
    expect(form.locator('[name=template_form]')).to_have_value('baby')
    expect(form.locator('[name=evolution]')).to_have_value('single-form')
    new_key = form.locator('[name=key]').input_value()
    assert new_key != 'baby'
    assert form.locator('[name=name]').input_value() != 'MINT'
    # The starter's zero unlock is not silently assigned to a distinct pet.
    form.locator('[name=unlock_prize]').select_option('1')
    form.get_by_role('button', name='Create and paint').click()
    page.wait_for_function("prefix => state.key?.startsWith(prefix) && state.mode === 'paint'", arg='creatures.' + new_key + '-', timeout=30000)
    after = json.loads((creator['source'] / 'assets.json').read_text())
    copied = [a for a in after['assets'] if a.get('form') == new_key]
    assert len(copied) == len(originals)
    for old in originals:
        new = next(a for a in copied if a['pose'] == old['pose'])
        assert Image.open(creator['source'] / old['path']).tobytes() == Image.open(creator['source'] / new['path']).tobytes()
        assert new['id'] != old['id']
    selected = page.evaluate('() => state.key')
    target = next(a for a in copied if a['key'] == selected)
    target_before = (creator['source'] / target['path']).read_bytes()
    expect(page.locator('#stage .paint-canvas canvas')).to_be_visible()
    paint_something(page, False)
    expect(page.locator('#paint-save-status')).to_have_text('Unsaved')
    page.locator('#save').click()
    expect(page.locator('#paint-save-status')).to_have_text('Saved')
    assert (creator['source'] / target['path']).read_bytes() != target_before
    assert all((creator['source'] / name).read_bytes() == value for name, value in source_bytes.items())


@pytest.mark.parametrize('full', [False, True])
def test_paint_duplicate_frame_preserves_source_ids_and_full_clip(creator, full):
    from PIL import Image
    page, source = creator['page'], creator['source']
    manifest_path = source / 'assets.json'
    manifest = json.loads(manifest_path.read_text())
    clip = next(c for c in manifest['clips'] if c['key'] == 'axolotl.idle')
    if full:
        while len(clip['frames']) < 6:
            clip['frames'].append(clip['frames'][-1])
            clip['durations_ms'].append(clip['durations_ms'][-1])
        manifest_path.write_text(json.dumps(manifest))
        page.reload()
        page.wait_for_function("() => typeof D !== 'undefined' && D.assets?.length > 0")
    before_clip = json.loads(json.dumps(clip))
    before_assets = json.loads(json.dumps(manifest['assets']))
    old_key = 'creatures.axolotl-idle-2'
    original = next(a for a in manifest['assets'] if a['key'] == old_key)
    original_bytes = (source / original['path']).read_bytes()
    page.get_by_role('tab', name='Paint', exact=True).click()
    page.evaluate('key => select(key)', old_key)
    expect(page.locator('#fb-duplicate')).to_be_visible()
    page.locator('#fb-duplicate').click()
    page.wait_for_function("old => state.key !== old && state.key?.startsWith('creatures.axolotl-')", arg=old_key, timeout=30000)
    expect(page.locator('#stage .paint-canvas canvas')).to_be_visible()
    new_key = page.evaluate('() => state.key')
    after = json.loads(manifest_path.read_text())
    new = next(a for a in after['assets'] if a['key'] == new_key)
    assert new['id'] not in {a['id'] for a in before_assets}
    assert after['assets'][:len(before_assets)] == before_assets
    assert (source / original['path']).read_bytes() == original_bytes
    assert Image.open(source / new['path']).tobytes() == Image.open(source / original['path']).tobytes()
    saved_clip = next(c for c in after['clips'] if c['key'] == clip['key'])
    if full:
        assert saved_clip == before_clip
        assert len(saved_clip['frames']) == 6
    else:
        position = before_clip['frames'].index(old_key) + 1
        assert saved_clip['frames'][position] == new_key
        assert len(saved_clip['frames']) == len(before_clip['frames']) + 1
