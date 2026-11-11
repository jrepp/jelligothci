"""Four-channel tracker authoring, real PCM audition and conflict-safe MOD saves."""
import sys
from pathlib import Path
import pytest
from playwright.sync_api import expect
from test_creation import creator  # noqa: F401

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import music_mod
from music_data import CATALOG


def open_music(page):
    page.evaluate("JelliShell.activateMode('music')")
    expect(page.locator('#mu-song')).to_be_visible(timeout=30000)
    page.locator('#mu-files summary').click()


def test_music_tracker_notes_draft_save_and_preview(creator):
    page=creator['page'];open_music(page)
    expect(page.locator('#mu-song option')).to_have_count(len(CATALOG))
    page.locator('#mu-song').select_option('tiny-thunder')
    expect(page.locator('#mu-title')).to_have_value('Tiny Thunder')
    page.locator('#mu-grid [data-row="2"][data-channel="0"]').click()
    page.keyboard.press('z')
    expect(page.locator('#mu-grid [data-row="3"][data-channel="0"]')).to_be_focused()
    page.locator('#mu-title').fill('My thunder')
    page.evaluate('JelliDrafts.flush()')
    page.reload();open_music(page)
    expect(page.locator('#mu-title')).to_have_value('My thunder',timeout=30000)
    page.locator('#mu-save').click();expect(page.locator('#mu-status')).to_have_text('Saved')
    doc=music_mod.decode((creator['content']/'music/tiny-thunder.mod').read_bytes())
    assert doc['title']=='My thunder' and doc['patterns'][0][2][0]['period']==428
    page.locator('#mu-play').click();expect(page.locator('#mu-playing')).to_contain_text('Playing',timeout=60000)
    page.locator('[data-mute="0"]').check()
    page.locator('#mu-pause').click();expect(page.locator('#mu-pause')).to_have_text('Resume')
    page.locator('#mu-stop').click();expect(page.locator('#mu-playing')).to_have_text('Stopped')


@pytest.mark.parametrize('width,height',[(1440,1000),(820,1180),(390,844)])
def test_music_layout_keyboard_and_conflict(creator,width,height):
    page=creator['page'];page.set_viewport_size({'width':width,'height':height});open_music(page)
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
    assert page.locator('.layout > aside').is_hidden()
    assert page.locator('main').bounding_box()['width'] >= width*.9
    page.locator('#mu-grid [data-row="0"][data-channel="0"]').focus()
    page.keyboard.press('ArrowRight');expect(page.locator('#mu-grid [data-row="0"][data-channel="1"]')).to_be_focused()
    page.locator('#mu-title').fill('Unsaved')
    path=creator['content']/'music/morning-tea.mod';doc=music_mod.decode(path.read_bytes());doc['title']='External';path.write_bytes(music_mod.encode(doc))
    page.locator('#mu-save').click();dialog=page.get_by_role('dialog');expect(dialog).to_be_visible()
    dialog.get_by_role('button',name='Cancel',exact=True).click();expect(page.locator('#mu-title')).to_have_value('Unsaved')
    assert music_mod.decode(path.read_bytes())['title']=='External'


def test_music_mod_export_sample_manager_and_accessibility(creator):
    from test_views import AXE
    page=creator['page'];open_music(page)
    page.add_script_tag(path=str(AXE))
    results=page.evaluate("async()=>await axe.run(document.querySelector('#music'))")
    assert results['violations']==[]
    page.locator('#mu-sample').select_option('6')
    page.locator('#mu-upload').set_input_files({'name':'pulse.pcm','mimeType':'application/octet-stream','buffer':bytes([0,127,0,129])*8})
    expect(page.locator('#mu-sample-name')).to_have_value('pulse.pcm')
    page.locator('#mu-loop-length').fill('32');page.locator('#mu-loop-length').press('Tab')
    with page.expect_download() as download:
        page.locator('#mu-export').click()
    doc=music_mod.decode(Path(download.value.path()).read_bytes())
    assert doc['samples'][5]['pcm'][:4]==[0,127,0,-127]
    assert doc['samples'][5]['loop_length']==32
    page.locator('#mu-grid [data-row="4"][data-channel="2"]').click()
    page.keyboard.press('q');expect(page.locator('#mu-grid [data-row="5"][data-channel="2"]')).to_be_focused()
    page.locator('#mu-arrange summary').click()
    page.locator('#mu-copy').click();page.locator('#mu-new').click();page.locator('#mu-paste').click()
    expect(page.locator('#mu-pattern')).to_have_value('2')


def test_music_workout_new_sample_palette(creator):
    page=creator['page'];open_music(page)
    page.locator('#mu-song').select_option('iron-pulse')
    expect(page.locator('#mu-title')).to_have_value('Iron Pulse')
    doc=music_mod.decode((creator['content']/'music/iron-pulse.mod').read_bytes())
    for name in ('Tight drive kick', 'Metal hat', 'Clap', 'Acid pulse bass', 'Sine sub bass'):
        slot=next(i for i,sample in enumerate(doc['samples'],1) if sample['name']==name)
        page.locator('#mu-sample').select_option(str(slot))
        expect(page.locator('#mu-sample-name')).to_have_value(name)
        expect(page.locator('#mu-loop-length')).to_have_value(str(doc['samples'][slot-1]['loop_length']))
    page.locator('#mu-sample-play').click()
    expect(page.locator('#mu-playing')).to_have_text('Auditioning sample')
    page.locator('#mu-stop').click()
    expect(page.locator('#mu-playing')).to_have_text('Stopped')


@pytest.mark.parametrize('width,height',[(320,740),(390,844),(820,1180)])
def test_music_touch_note_entry_and_scroll(creator,width,height):
    page=creator['page'];page.set_viewport_size({'width':width,'height':height});open_music(page)
    page.locator('#mu-entry-sample').select_option('2')
    page.locator('#mu-step').fill('2');page.locator('#mu-step').press('Tab')
    page.locator('#mu-octave').fill('3');page.locator('#mu-octave').press('Tab')
    page.locator('#mu-next-channel').click()
    from touch import cdp, tap
    cdp(page).send('Emulation.setTouchEmulationEnabled', {'enabled':True})
    page.locator('#mu-key-0').scroll_into_view_if_needed()
    box=page.locator('#mu-key-0').bounding_box()
    tap(page,[(box['x']+box['width']/2,box['y']+box['height']/2)])
    expect(page.locator('#mu-editor legend')).to_have_text('Selected cell: row 2, channel 2')
    expect(page.locator('#mu-key-0')).to_be_focused()
    expect(page.locator('[data-row="0"][data-channel="1"]')).to_contain_text('C-3 02')
    page.locator('#mu-prev-row').click()
    page.locator('#mu-key-1').click()
    page.locator('#mu-save').click()
    expect(page.locator('#mu-status')).to_have_text('Saved')
    doc=music_mod.decode((creator['content']/'music/morning-tea.mod').read_bytes())
    assert doc['patterns'][0][0][1]['period']==214
    assert doc['patterns'][0][1][1]['period']==202
    assert doc['patterns'][0][1][1]['sample']==2
    page.locator('.mu-scroll').evaluate('(el)=>{el.scrollTop=900;el.scrollLeft=140}')
    before=page.locator('.mu-scroll').evaluate('(el)=>[el.scrollTop,el.scrollLeft]')
    page.locator('#mu-param').fill('12');page.locator('#mu-param').press('Tab')
    assert page.locator('.mu-scroll').evaluate('(el)=>[el.scrollTop,el.scrollLeft]')==before
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
    if width<=390:
        assert page.locator('#mu-key-0').bounding_box()['height']>=44
        assert page.locator('#mu-grid [data-row="0"][data-channel="0"]').bounding_box()['height']>=44


def test_music_sample_presets_replace_undo_and_export(creator):
    page=creator['page'];open_music(page)
    original=page.locator('#mu-sample-name').input_value()
    page.locator('#mu-sounds summary').click()
    page.locator('#mu-preset').select_option('velvet-keys')
    page.locator('#mu-load-preset').click()
    expect(page.locator('#mu-sample-name')).to_have_value('Velvet keys')
    page.locator('#mu-undo').click()
    expect(page.locator('#mu-sample-name')).to_have_value(original)
    page.locator('#mu-sounds summary').click()
    page.locator('#mu-load-preset').click()
    page.locator('#mu-save').click()
    expect(page.locator('#mu-status')).to_have_text('Saved')
    doc=music_mod.decode((creator['content']/'music/morning-tea.mod').read_bytes())
    assert doc['samples'][0]['name']=='Velvet keys'
    assert doc['samples'][0]['loop_length']>0
    page.set_viewport_size({'width':390,'height':844})
    page.locator('#mu-editor').scroll_into_view_if_needed()
    from conftest import OUT
    page.screenshot(path=str(OUT/'screenshots/music-editor-phone.png'))


@pytest.mark.parametrize('width',[320,390,820])
def test_music_mobile_sound_browser_preview_and_entry(creator,width):
    from conftest import OUT
    page=creator['page'];page.set_viewport_size({'width':width,'height':844});open_music(page)
    original=music_mod.decode((creator['content']/'music/morning-tea.mod').read_bytes())
    page.locator('#mu-sounds summary').click()
    page.locator('#mu-sound-search').fill('no such sound')
    expect(page.locator('#mu-preset-preview')).to_be_disabled()
    expect(page.locator('#mu-add-preset')).to_be_disabled()
    page.locator('#mu-sound-search').fill('velvet')
    expect(page.locator('#mu-preset option')).to_have_count(1)
    page.locator('#mu-preset-preview').click()
    expect(page.locator('#mu-sound-status')).to_contain_text('Previewing Velvet keys')
    expect(page.locator('#mu-status')).to_have_text('Saved')
    page.locator('#mu-preset-stop').click()
    expect(page.locator('#mu-sound-status')).to_have_text('Stopped')
    page.locator('#mu-sound-search').fill('')
    page.locator('#mu-sound-category').select_option('Bass')
    assert page.locator('#mu-preset option').count()>1
    expect(page.locator('#mu-preset-info')).to_contain_text('Bass')
    if width==390:
        page.locator('#mu-sound-category').scroll_into_view_if_needed()
        page.screenshot(path=str(OUT/'screenshots/music-sound-browser-phone.png'))
    from test_views import AXE
    page.add_script_tag(path=str(AXE))
    assert page.evaluate("async()=> (await axe.run(document.querySelector('#mu-editor'))).violations")==[]
    name=page.locator('#mu-preset option:checked').inner_text()
    page.locator('#mu-add-preset').click()
    expect(page.locator('#mu-sounds')).not_to_have_attribute('open','')
    expect(page.locator('#mu-key-0')).to_be_focused()
    slot=int(page.locator('#mu-entry-sample').input_value())
    assert not original['samples'][slot-1]['pcm']
    page.keyboard.press('Space')
    expect(page.locator('[data-row="0"][data-channel="0"]')).to_contain_text(f'{slot:02X}')
    page.locator('#mu-save').click();expect(page.locator('#mu-status')).to_have_text('Saved')
    doc=music_mod.decode((creator['content']/'music/morning-tea.mod').read_bytes())
    assert doc['samples'][slot-1]['name']==name
    for i,sample in enumerate(original['samples']):
        if i!=slot-1:
            assert doc['samples'][i]==sample
    assert doc['patterns'][0][0][0]['sample']==slot
    assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
