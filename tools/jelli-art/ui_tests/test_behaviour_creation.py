"""Behaviour copies preserve authored data and appearance without adding reaction rules."""
import json

from playwright.sync_api import expect

from test_creation import creator  # noqa: F401


def open_behaviours(page):
    page.get_by_role('tab', name='Behaviour', exact=True).click()
    page.locator('#bv-tabs [data-tab=states]').click()
    expect(page.locator('#bv-duplicate-state')).to_be_visible()


def save_behaviour(page):
    page.locator('#bv-save').click()
    expect(page.locator('#bv-save-status')).to_have_text('Saved', timeout=30000)


def test_duplicate_behaviour_saves_exact_copy_then_edits_without_aliasing(creator):
    page, content = creator['page'], creator['content']
    before = json.loads((content / 'behaviors.json').read_text())
    appearances = json.loads((content / 'creatures.json').read_text())
    source = before['states'][0]
    source_look = next(look for look in appearances['state_presentation'] if look['state'] == source['name'])
    open_behaviours(page)
    page.locator(f'#bv-states [data-state="{source["name"]}"]').click()
    page.locator('#bv-duplicate-state').click()
    dialog = page.locator('dialog[open]')
    name = source['name'] + '_copy'
    expect(dialog.locator('#bv-copy-name')).to_have_value(name)
    dialog.locator('[value=copy]').click()
    expect(page.locator('#bv-state-summary')).to_contain_text('Not triggered yet')
    save_behaviour(page)
    saved = json.loads((content / 'behaviors.json').read_text())
    saved_looks = json.loads((content / 'creatures.json').read_text())['state_presentation']
    assert saved['states'][:-1] == before['states']
    assert saved['states'][-1] == {**source, 'name': name}
    assert saved_looks[:-1] == appearances['state_presentation']
    assert saved_looks[-1] == {**source_look, 'state': name}
    assert saved['repertoires'] == before['repertoires']
    duration = page.get_by_role('spinbutton', name='Shortest duration, seconds', exact=True)
    duration.fill(str(source['duration_s'][0] + 1))
    duration.press('Tab')
    caption = page.get_by_role('textbox', name='Caption, upper case, at most 16', exact=True)
    caption.fill('COPY ONLY')
    caption.press('Tab')
    save_behaviour(page)
    edited = json.loads((content / 'behaviors.json').read_text())
    edited_looks = json.loads((content / 'creatures.json').read_text())['state_presentation']
    assert edited['states'][:-1] == before['states']
    assert edited['repertoires'] == before['repertoires']
    assert next(look for look in edited_looks if look['state'] == source['name']) == source_look
    assert edited['states'][-1]['duration_s'][0] == source['duration_s'][0] + 1
    assert edited_looks[-1]['caption'] == 'COPY ONLY'
    page.reload()
    page.get_by_role('tab', name='Behaviour', exact=True).click()
    page.locator('#bv-tabs [data-tab=states]').click()
    page.locator(f'#bv-states [data-state="{name}"]').click()
    expect(page.get_by_role('textbox', name='Caption, upper case, at most 16', exact=True)).to_have_value('COPY ONLY')


def test_duplicate_behaviour_invalid_and_existing_keys_keep_dialog_and_sources(creator):
    page, content = creator['page'], creator['content']
    paths = [content / 'behaviors.json', content / 'creatures.json']
    before = [path.read_bytes() for path in paths]
    original = json.loads(before[0])['states'][0]['name']
    open_behaviours(page)
    page.locator('#bv-duplicate-state').click()
    dialog = page.locator('dialog[open]')
    name = dialog.locator('#bv-copy-name')
    name.fill('BAD KEY')
    expect(dialog.locator('#bv-copy-error')).to_contain_text('lower_snake_case')
    dialog.locator('[value=copy]').click()
    expect(dialog).to_be_visible()
    name.fill(original)
    expect(dialog.locator('#bv-copy-error')).to_contain_text('already uses that key')
    name.press('Enter')
    expect(dialog).to_be_visible()
    name.fill('keyboard_copy')
    name.press('Enter')
    expect(dialog).to_be_hidden()
    expect(page.locator('#bv-state-summary')).to_contain_text('keyboard copy')
    assert [path.read_bytes() for path in paths] == before


def test_duplicate_behaviour_capacity_is_visible_and_preserves_catalog(creator):
    page, content = creator['page'], creator['content']
    behavior_path, appearance_path = content / 'behaviors.json', content / 'creatures.json'
    doc, appearances = json.loads(behavior_path.read_text()), json.loads(appearance_path.read_text())
    for index in range(16 - len(doc['states'])):
        name = f'capacity_{index}'
        doc['states'].append({**doc['states'][0], 'name': name})
        appearances['state_presentation'].append({**appearances['state_presentation'][0], 'state': name})
    behavior_path.write_text(json.dumps(doc))
    appearance_path.write_text(json.dumps(appearances))
    before = behavior_path.read_bytes(), appearance_path.read_bytes()
    page.reload()
    open_behaviours(page)
    expect(page.locator('#bv-duplicate-state')).to_be_disabled()
    expect(page.locator('#bv-add-state')).to_be_disabled()
    assert (behavior_path.read_bytes(), appearance_path.read_bytes()) == before
