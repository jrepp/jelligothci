"""Keep unfinished numeric edits out of authored behavior and activity documents."""
import json

import pytest
from playwright.sync_api import expect
from test_creation import creator  # noqa: F401


def mode(page, name):
    page.evaluate('name => JelliShell.activateMode(name)', name)


@pytest.mark.parametrize('width', [1440,390])
def test_behavior_invalid_numeric_keeps_context_and_disk(creator, width):
    page=creator['page']
    page.set_viewport_size({'width':width,'height':900})
    path=creator['content']/'behaviors.json'
    before=path.read_bytes()
    mode(page,'behaviour')
    field=page.get_by_role('spinbutton',name='Shortest duration, seconds',exact=True)
    original=field.input_value()
    selected=page.locator('#bv-states [aria-pressed=true]').get_attribute('data-state')
    field.fill('')
    field.press('Tab')
    page.locator('#bv-tabs [data-tab=repertoires]').click()
    expect(page.locator('#bv-tabs [data-tab=states]')).to_have_attribute('aria-pressed','true')
    expect(field).to_have_value('')
    page.evaluate('Studio.saveContent()')
    assert path.read_bytes()==before
    field.fill(original)
    field.press('Tab')
    page.locator('#bv-tabs [data-tab=repertoires]').click()
    expect(page.locator('#bv-tabs [data-tab=repertoires]')).to_have_attribute('aria-pressed','true')
    page.locator('#bv-tabs [data-tab=states]').click()
    expect(page.locator('#bv-states [aria-pressed=true]')).to_have_attribute('data-state',selected)


def test_activity_blank_and_stepper_recovery_preserve_optional_bonus(creator):
    page=creator['page']
    mode(page,'activities')
    field=page.locator('[data-field=duration_s]')
    field.fill('')
    field.press('Tab')
    page.locator('[data-activity="1"]').click()
    expect(page.locator('[data-activity="0"]')).to_have_attribute('aria-pressed','true')
    expect(field).to_have_value('')
    field.locator('..').get_by_role('button',name='Increase Duration (seconds)',include_hidden=True).dispatch_event('click')
    expect(page.locator('[data-field=duration_s]')).to_have_value('1')
    bonus=page.locator('[data-bonus]').first
    bonus.fill('')
    bonus.press('Tab')
    page.locator('#ac-save').click()
    expect(page.locator('#ac-save-status')).to_have_text('Saved activity recipes')
    doc=json.loads((creator['content']/'activities.json').read_text())
    assert doc['moments'][0]['duration_s']==1
    assert all(b['form']!=0 for b in doc['moments'][0]['bonuses'])


def test_creature_simulator_rejects_out_of_range_pet_id(creator):
    page=creator['page']
    mode(page,'creature')
    field=page.locator('#bh-pet')
    field.fill('1000')
    page.locator('#bh-sim').click()
    expect(field).to_have_value('1000')
    expect(field).to_be_focused()
    field.fill('999')
    page.locator('#bh-sim').click()
    expect(field).to_have_value('999')
