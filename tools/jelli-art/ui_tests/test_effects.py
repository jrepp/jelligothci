"""Celebration timeline authoring through real copied content."""
import json
import pytest
from playwright.sync_api import expect
from test_creation import creator  # noqa: F401


def open_effects(page):
    page.evaluate("JelliShell.activateMode('effects')")
    expect(page.locator('#fx-canvas')).to_be_visible()


def test_effect_authoring_draft_save_and_paint(creator):
    page=creator['page']
    open_effects(page)
    page.locator('#fx-name').fill('A new celebration')
    page.locator('#fx-point-x').fill('7')
    page.evaluate('JelliDrafts.flush()')
    page.reload()
    open_effects(page)
    expect(page.locator('#fx-name')).to_have_value('A new celebration')
    expect(page.locator('#fx-point-x')).to_have_value('7')
    page.locator('#fx-save').click()
    expect(page.locator('#fx-status')).to_have_text('Saved')
    data=json.loads((creator['content']/'effects.json').read_text())
    assert data['effects'][0]['name']=='A new celebration'
    assert data['hardpoints'][0]['positions']['baby']==[7,0]
    before=page.locator('#fx-canvas').evaluate('(c)=>c.toDataURL()')
    page.locator('#fx-scrub').fill('1000')
    page.wait_for_timeout(100)
    assert page.locator('#fx-canvas').evaluate('(c)=>c.toDataURL()')!=before
    page.locator('#fx-add-track').click()
    page.locator('[data-text="name"]').fill('My sparkle')
    page.locator('#fx-add-frame').click()
    expect(page.locator('[data-frame-asset]')).to_have_count(2)
    page.locator('#fx-undo').click()
    expect(page.locator('[data-frame-asset]')).to_have_count(1)
    page.locator('[data-paint="0"]').click()
    expect(page.locator('#stage .paint-canvas canvas')).to_be_visible()


@pytest.mark.parametrize('width,height',[(1440,1000),(390,844)])
def test_effect_hardpoints_layout_and_conflict(creator,width,height):
    page=creator['page'];page.set_viewport_size({'width':width,'height':height})
    open_effects(page)
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
    page.locator('#fx-form').select_option('axolotl')
    page.locator('#fx-point').select_option('1')
    page.locator('#fx-point-y').fill('-22')
    path=creator['content']/'effects.json'
    doc=json.loads(path.read_text());doc['effects'][0]['name']='External edit';path.write_text(json.dumps(doc))
    page.locator('#fx-save').click()
    dialog=page.get_by_role('dialog');expect(dialog).to_be_visible()
    dialog.get_by_role('button',name='Cancel',exact=True).click()
    expect(page.locator('#fx-point-y')).to_have_value('-22')
    assert json.loads(path.read_text())['effects'][0]['name']=='External edit'


def test_effect_preview_accessibility_and_engine_selection(creator):
    from test_views import AXE
    from conftest import OUT
    page=creator['page'];open_effects(page)
    page.locator('#fx-scrub').fill('1000')
    page.wait_for_timeout(100)
    (OUT/'screenshots').mkdir(parents=True,exist_ok=True)
    page.locator('#fx-canvas').screenshot(path=str(OUT/'screenshots/effects-canvas.png'))
    page.evaluate("document.querySelector('main').scrollTop=0")
    page.screenshot(path=str(OUT/'screenshots/effects-workspace.png'))
    page.add_script_tag(path=str(AXE))
    violations=page.evaluate("""async () => (await axe.run(document.querySelector('#effects'),
      {runOnly:{type:'tag',values:['wcag2a','wcag2aa']}})).violations.filter(v=>['serious','critical'].includes(v.impact))""")
    assert violations==[],violations
    page.locator('#fx-form').select_option('axolotl')
    page.locator('#fx-engine').click()
    expect(page.locator('#gp-form [data-k="effect"]')).to_have_value('celebrate')
    expect(page.locator('#gp-form [data-k="form"]')).to_have_value('AXOLOTL')


def test_effect_save_keeps_newer_edits_and_new_cues(creator):
    page=creator['page'];open_effects(page)
    page.locator('#fx-add-cue').click()
    expect(page.locator('[data-fx="start_ms"]')).to_have_value('3000')
    expect(page.locator('#fx-duration')).to_have_value('3500')
    pending=[]
    page.route('**/api/effects',lambda r:pending.append(r) if r.request.method=='POST' else r.continue_())
    page.locator('#fx-save').click()
    expect(page.locator('#fx-status')).to_have_text('Saving…')
    page.locator('#fx-name').fill('Newer edit')
    pending[0].continue_()
    expect(page.locator('#fx-status')).to_have_text('Unsaved changes')
    expect(page.locator('#fx-name')).to_have_value('Newer edit')


def test_new_pet_hardpoints_join_effect_workspace(creator):
    page=creator['page'];open_effects(page)
    page.evaluate("""async () => {
      await Studio.api('POST','/api/create',{artist:'UI test',bases:D.creation.bases,
        request:{type:'pet',key:'celebration-pet',name:'PARTY PET',template_form:'baby',size:32,
          evolution:'single-form',gameplay_policy:'copy-template',behavior:'jelly',scale:3,
          icon_scale:1,portrait_scale:3,unlock_prize:1,hint:'CATCH BUTTERFLY'}});
      await Studio.reload();
    }""")
    open_effects(page)
    assert page.evaluate("D.assets.some(a=>a.form==='celebration-pet')")
    assert 'celebration-pet' in page.locator('#fx-form').text_content(), page.locator('#fx-status').text_content()
    page.locator('#fx-form').select_option('celebration-pet')
    page.locator('#fx-point').select_option('1')
    expect(page.locator('#fx-point-y')).to_have_value('-20')


def test_gameplay_bindings_save_and_undo(creator):
    page=creator['page'];open_effects(page)
    binding=page.get_by_role('combobox',name='Pet evolves',exact=True)
    expect(binding).to_have_value('evolution')
    binding.select_option('celebrate')
    page.locator('#fx-undo').click()
    expect(binding).to_have_value('evolution')
    page.locator('#fx-redo').click()
    expect(binding).to_have_value('celebrate')
    page.locator('#fx-save').click()
    expect(page.locator('#fx-status')).to_have_text('Saved')
    data=json.loads((creator['content']/'effects.json').read_text())
    assert data['triggers']['evolution']=='celebrate'
    assert data['triggers']['present-caught']=='gift-acquired'
    assert data['triggers']['present-accepted']=='present-accepted'
    page.reload();open_effects(page)
    expect(binding).to_have_value('celebrate')


def test_effect_reload_waits_for_inflight_content(creator):
    page=creator['page'];open_effects(page)
    pending=[]
    def hold_first(route):
        if not pending:
            pending.append(route)
        else:
            route.continue_()
    page.route('**/api/effects',hold_first)
    with page.expect_request('**/api/effects'):
        page.evaluate('() => { window.firstEffectReload=Studio.reload(); }')
    for _ in range(100):
        if pending:break
        page.wait_for_timeout(20)
    assert pending
    older=pending[0].fetch()
    # Deliver an older in-flight response after a newer saved document exists.
    path=creator['content']/'effects.json'
    doc=json.loads(path.read_text());doc['effects'][0]['name']='Newest celebration'
    path.write_text(json.dumps(doc))
    page.evaluate('() => { window.secondEffectReload=Studio.reload(); }')
    pending[0].fulfill(response=older)
    page.evaluate('() => Promise.all([window.firstEffectReload,window.secondEffectReload])')
    expect(page.locator('#fx-name')).to_have_value('Newest celebration')
