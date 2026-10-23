"""Contextual painting keeps layers, saved art and game attachment metadata separate."""
import json
import pytest
from playwright.sync_api import expect
from conftest import OUT
from test_creation import creator  # noqa: F401
from test_paint_transactions import dot, rgba


@pytest.mark.parametrize('width,height', [(1440, 1000), (390, 844)])
def test_layer_roundtrip_and_context(creator, width, height):
    page = creator['page']
    page.set_viewport_size({'width': width, 'height': height})
    page.evaluate("() => {state.key=D.assets.find(a=>a.kind==='icons').key; JelliShell.activateMode('review');}")
    page.locator('#review-paint').click()
    expect(page.locator('#paint-back')).to_have_text('← Back to review')
    expect(page.locator('aside')).to_be_hidden()
    expect(page.locator('header')).to_be_hidden()
    page.locator('#paint-browse').click()
    expect(page.locator('aside')).to_be_visible()
    page.locator('#paint-library-close').click()
    page.locator('#paint-workspace').click()
    expect(page.get_by_role('tab', name='Paint', exact=True)).to_be_visible()
    page.locator('#paint-workspace').click()
    expect(page.locator('#paint-more')).not_to_have_attribute('open', '')
    (OUT / 'screenshots').mkdir(parents=True, exist_ok=True)
    page.locator('#paint-back').scroll_into_view_if_needed()
    page.screenshot(path=str(OUT / 'screenshots' / f'context-paint-{width}.png'), animations='disabled')
    original = rgba(page)
    page.locator('#paint-layer-add').click()
    page.locator('#paint-layer-name').fill('Highlights')
    page.locator('#paint-layer-name').press('Tab')
    dot(page, 0)
    painted = rgba(page)
    assert painted != original
    page.locator('[data-lock="1"]').click()
    dot(page, 1)
    assert rgba(page) == painted, 'Locked layers cannot be painted'
    page.locator('[data-visible="1"]').click()
    assert rgba(page) == original, 'Hiding a layer reveals the original artwork'
    page.locator('[data-visible="1"]').click()
    page.locator('#save').click()
    expect(page.locator('#paint-save-status')).to_have_text('Saved')
    key = page.evaluate('state.key')
    page.reload()
    page.evaluate('(key)=>Studio.openPaint(key,{label:"review"})', key)
    expect(page.locator('[data-layer]')).to_have_count(2)
    expect(page.locator('#paint-layer-name')).to_have_value('Highlights')
    assert rgba(page) == painted
    page.locator('#paint-layer-delete').click()
    assert rgba(page) == original
    page.locator('#undo').click()
    assert rgba(page) == painted
    page.locator('#paint-back').click()
    expect(page.locator('#review-paint')).to_be_visible()
    if width >= 900:
        expect(page.locator('aside')).to_be_visible()
    assert page.evaluate('() => document.documentElement.scrollWidth <= innerWidth')


def test_attachment_placement_saves_metadata_not_pixels(creator):
    page = creator['page']
    page.evaluate("JelliShell.activateMode('effects')")
    page.locator('#fx-paint-pet').click()
    expect(page.locator('#paint-attachments')).to_have_attribute('open', '')
    before = rgba(page)
    page.locator('#paint-attachment-place').click()
    dot(page, 3)
    assert rgba(page) == before
    assert not page.evaluate('Studio.paintDirty(state.key)')
    page.locator('#paint-attachment-x').fill('5')
    page.locator('#paint-attachment-x').press('Tab')
    page.locator('#paint-attachment-save').click()
    expect(page.locator('#paint-attachment-save')).to_be_disabled()
    form = page.evaluate('asset().form')
    data = json.loads((creator['content'] / 'effects.json').read_text())
    assert data['hardpoints'][0]['positions'][form][0] == 5
    page.locator('#paint-back').click()
    expect(page.locator('#fx-point-x')).to_have_value('5')
    assert rgba(page) == before


def test_layer_draft_recovers_without_flattening(creator):
    page = creator['page']
    page.evaluate("() => Studio.openPaint(D.assets.find(a=>a.kind==='icons').key)")
    page.locator('#paint-layer-add').click()
    dot(page, 0)
    before = rgba(page)
    key = page.evaluate('state.key')
    page.evaluate('JelliDrafts.flush()')
    page.reload()
    page.wait_for_function('() => window.Studio?.layerDocument && window.JelliDrafts && D.assets?.length')
    page.evaluate('(key)=>Studio.openPaint(key)', key)
    expect(page.locator('[data-layer]')).to_have_count(2)
    assert rgba(page) == before
    assert page.evaluate('Studio.paintDirty(state.key)')
    page.locator('#undo').click()
    expect(page.locator('[data-layer]')).to_have_count(1)
