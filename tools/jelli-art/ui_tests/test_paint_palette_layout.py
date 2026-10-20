"""Palette colour wells and Swap stay within the inspector at both input densities."""
import pytest
from playwright.sync_api import expect

from conftest import OUT


@pytest.mark.parametrize('viewport', ['desktop', 'tablet', 'ipad-landscape', 'ipad-portrait'])
def test_colour_pair_and_swap_fit_inspector_before_and_after_swap(open_view, viewport):
    page = open_view('paint-creature', viewport)
    swap = page.locator('#swap-colours')
    swap.scroll_into_view_if_needed()
    before = page.evaluate('() => [state.color, state.color2]')
    for swapped in (False, True):
        dimensions = page.evaluate("""() => {
          const side = document.querySelector('.paint-side'), boundary = side.getBoundingClientRect();
          const style = getComputedStyle(side), left = boundary.left + parseFloat(style.paddingLeft),
            right = boundary.right - parseFloat(style.paddingRight);
          return ['.paint-pair', '#paint-main-colour', '#paint-secondary-colour', '#swap-colours'].map(selector => {
            const el = document.querySelector(selector), r = el.getBoundingClientRect();
            return {selector, left: r.left, right: r.right, width: r.width, height: r.height, limitLeft: left, limitRight: right};
          });
        }""")
        for control in dimensions:
            assert control['left'] >= control['limitLeft'] - 1, (viewport, swapped, control)
            assert control['right'] <= control['limitRight'] + 1, (viewport, swapped, control)
        if viewport.startswith('ipad'):
            button = next(control for control in dimensions if control['selector'] == '#swap-colours')
            assert button['width'] >= 44 and button['height'] >= 44
        if not swapped:
            swap.click()
            assert page.evaluate('() => [state.color, state.color2]') == before[::-1]
            expect(swap).to_be_visible()
    (OUT / 'screenshots').mkdir(parents=True, exist_ok=True)
    page.screenshot(path=str(OUT / 'screenshots' / f'paint-palette-contained@{viewport}.png'), animations='disabled')
