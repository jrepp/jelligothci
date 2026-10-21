"""The header rail stays usable without scrolling or covering the workspace."""
import pytest
from playwright.sync_api import expect

from conftest import OUT, settle


@pytest.mark.parametrize("width,height,touch", [(320, 740, True), (390, 844, True),
                                                (820, 1180, True), (1180, 820, True),
                                                (1440, 900, False), (1920, 1080, False)])
def test_responsive_navigation(browser, studio, width, height, touch):
    context = browser.new_context(viewport={"width": width, "height": height}, has_touch=touch,
                                  reduced_motion="reduce", color_scheme="dark")
    try:
        page = settle(context, studio, "paint-creature", "phone")
        rail = page.locator('#shell-modes [role=tablist]')
        # Tools are all discoverable without an invisible second scrolling rail.
        if width <= 760:
            for tool in page.locator('.shell-tools button:visible, .shell-tools select').all():
                box = tool.bounding_box()
                assert box["x"] >= 0 and box["x"] + box["width"] <= width
                assert box["height"] >= 44
            assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
        # Edge controls scroll the rail without changing the chosen workspace.
        more = page.get_by_role("button", name="More modes", exact=True)
        if more.is_visible() and more.is_enabled():
            offset = rail.evaluate("el => el.scrollLeft")
            active = rail.locator('[aria-selected=true]').get_attribute("id")
            more.click()
            page.wait_for_function("offset => document.querySelector('#shell-modes [role=tablist]').scrollLeft > offset", arg=offset)
            assert rail.locator('[aria-selected=true]').get_attribute("id") == active
        assert page.evaluate("document.scrollingElement.scrollTop") == 0
        # Motion is opt-in to the user's preference, while selection remains visible.
        assert rail.get_by_role("tab").first.evaluate("el => parseFloat(getComputedStyle(el).transitionDuration)") <= 0.00001
        page.emulate_media(reduced_motion="no-preference")
        assert rail.get_by_role("tab").first.evaluate("el => parseFloat(getComputedStyle(el).transitionDuration)") > 0.00001
        page.emulate_media(reduced_motion="reduce")
        # Keyboard navigation exposes the last tab, then wraps back to the first.
        selected = rail.locator('[aria-selected=true]')
        selected.focus()
        selected.press("End")
        last = rail.get_by_role("tab").last
        expect(last).to_be_focused()
        expect(last).to_have_attribute("aria-selected", "true")
        page.wait_for_timeout(80)
        assert_inside(last.bounding_box(), rail.bounding_box())
        last.press("ArrowRight")
        first = rail.get_by_role("tab").first
        expect(first).to_be_focused()
        expect(first).to_have_attribute("aria-selected", "true")
        page.wait_for_timeout(80)
        assert_inside(first.bounding_box(), rail.bounding_box())
        # Rotate/shrink with a far-right workspace selected: it must remain visible.
        first.press("End")
        page.set_viewport_size({"width": 390 if width > 760 else 820, "height": height})
        page.wait_for_timeout(100)
        assert_inside(last.bounding_box(), rail.bounding_box())
        page.set_viewport_size({"width": width, "height": height})
        page.evaluate("JelliShell.activateMode('paint')")
        page.wait_for_timeout(100)
        OUT.joinpath("navigation").mkdir(exist_ok=True)
        page.screenshot(path=str(OUT / "navigation" / f"nav-{width}-clean.png"))
        # Dirty labels and a long remote status must not widen the header or shift the canvas.
        before = page.locator('header').bounding_box()["height"]
        page.evaluate("""() => {
          JelliShell.registerDirty('nav-test', {label:'Paint and a very long unsaved asset name', mode:'paint', check:() => 1});
          document.getElementById('git-status').textContent = 'A long repository synchronization message';
        }""")
        assert page.locator('header').bounding_box()["height"] == before
        assert page.locator('header').bounding_box()["width"] <= width
        OUT.joinpath("navigation").mkdir(exist_ok=True)
        page.screenshot(path=str(OUT / "navigation" / f"nav-{width}.png"))
    finally:
        context.close()


def assert_inside(tab, rail):
    assert tab["x"] >= rail["x"] - 1
    assert tab["x"] + tab["width"] <= rail["x"] + rail["width"] + 1
