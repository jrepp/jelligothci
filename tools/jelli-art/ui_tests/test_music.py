"""Four-channel tracker authoring, real PCM audition and conflict-safe MOD saves."""
import sys
from pathlib import Path
import pytest
from playwright.sync_api import expect
from test_creation import creator  # noqa: F401

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import music_mod


def open_music(page):
    page.evaluate("JelliShell.activateMode('music')")
    expect(page.locator('#mu-song')).to_be_visible(timeout=30000)


def test_music_tracker_notes_draft_save_and_preview(creator):
    page=creator['page'];open_music(page)
    expect(page.locator('#mu-song option')).to_have_count(7)
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
    page.locator('#mu-copy').click();page.locator('#mu-new').click();page.locator('#mu-paste').click()
    expect(page.locator('#mu-pattern')).to_have_value('2')
