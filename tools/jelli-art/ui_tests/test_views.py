"""Every studio view at desktop, tablet and phone sizes: accessibility-tree snapshots, a few
screenshots, an axe-core pass against a baseline, and the chrome budget (report only).

Run: tools/jelli-art/ui_tests/run.sh   (JELLI_UI_UPDATE=1 rewrites the baselines)
"""
import json

import pytest
from PIL import Image, ImageChops
from playwright.sync_api import expect

from conftest import (BASELINES, BUDGET, CASES, FIXTURE, HERE, OUT, SCREENSHOTS, UPDATE, VIEWS, VOLATILE,
                      case_id)

# Landmarks whose accessibility tree is snapshotted; the header holds checkout-dependent items.
REGIONS = {"modes": "#shell-modes", "assets": "aside", "main": "main"}
AXE = HERE / "vendor/axe-core/axe.min.js"
AXE_BASELINE = BASELINES / "axe.json"
GATED = {"serious", "critical"}
# A channel difference above TOLERANCE counts as a changed pixel; more than MAX_CHANGED of them fails.
TOLERANCE, MAX_CHANGED = 24, 0.002


def aria_check(page, case):
    for name, selector in REGIONS.items():
        region = page.locator(selector).first
        path = BASELINES / "aria" / case / f"{name}.yml"
        if not region.is_visible():
            text = "# not visible\n"
            if UPDATE:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            else:
                assert path.read_text() == text, f"{case} {name}: region is hidden now but visible in the baseline"
            continue
        if UPDATE:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(region.aria_snapshot() + "\n")
        else:
            assert path.exists(), f"no aria baseline {path.relative_to(HERE)}; run with JELLI_UI_UPDATE=1"
            text = path.read_text()
            if text.startswith("# not visible"):
                pytest.fail(f"{case} {name}: region is visible now but hidden in the baseline")
            expect(region).to_match_aria_snapshot(text)


def screenshot_check(page, case):
    if case not in FIXTURE["screenshots"]:
        return
    actual_path = OUT / "screenshots" / f"{case}.png"
    actual_path.parent.mkdir(parents=True, exist_ok=True)
    page.screenshot(path=str(actual_path), animations="disabled", caret="hide",
                    mask=[page.locator(s) for s in VOLATILE], mask_color="#808080")
    baseline = BASELINES / "screenshots" / f"{case}.png"
    if not SCREENSHOTS:
        return  # only the linux/amd64 Playwright image (CI) renders the reference pixels
    if UPDATE:
        baseline.parent.mkdir(parents=True, exist_ok=True)
        baseline.write_bytes(actual_path.read_bytes())
        return
    assert baseline.exists(), f"no screenshot baseline for {case}; copy it from the CI ui-tests artifact"
    want, got = Image.open(baseline).convert("RGB"), Image.open(actual_path).convert("RGB")
    assert want.size == got.size, f"{case}: screenshot is {got.size}, baseline {want.size}"
    diff = ImageChops.difference(want, got)
    changed = sum(1 for px in diff.getdata() if max(px) > TOLERANCE)
    share = changed / (want.width * want.height)
    if share > MAX_CHANGED:
        mask = diff.point(lambda v: 255 if v > TOLERANCE else 0)
        mask.save(OUT / "screenshots" / f"{case}.diff.png")
        pytest.fail(f"{case}: {changed} px ({share:.2%}) differ from the baseline; see {OUT / 'screenshots'}")


def axe_check(page, case, baseline):
    page.add_script_tag(path=str(AXE))
    violations = page.evaluate("""async () => (await axe.run(document, {resultTypes: ['violations']})).violations
        .map(v => ({id: v.id, impact: v.impact, help: v.help, nodes: v.nodes.length,
                    targets: v.nodes.slice(0, 5).map(n => n.target.join(' '))}))""")
    report = OUT / "axe" / f"{case}.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(violations, indent=2) + "\n")
    gated = {v["id"]: v["nodes"] for v in violations if v["impact"] in GATED}
    if UPDATE:
        baseline[case] = dict(sorted(gated.items()))
        return
    known = baseline.get(case, {})
    worse = [f"{rule} ({count} nodes, baseline {known.get(rule, 0)})" for rule, count in gated.items()
             if count > known.get(rule, 0)]
    assert not worse, f"{case}: new serious/critical axe violations: {', '.join(worse)}; see {report}"


def budget_measure(page, view, case):
    BUDGET[case] = page.evaluate("""selector => {
        const el = document.querySelector(selector); if (!el) return 0;
        const r = el.getBoundingClientRect(), vw = innerWidth, vh = innerHeight;
        const w = Math.max(0, Math.min(r.right, vw) - Math.max(r.left, 0)), h = Math.max(0, Math.min(r.bottom, vh) - Math.max(r.top, 0));
        return Math.round(w * h / (vw * vh) * 1000) / 1000; }""", VIEWS[view]["primary"])


@pytest.fixture(scope="module")
def axe_baseline():
    baseline = json.loads(AXE_BASELINE.read_text()) if AXE_BASELINE.exists() else {}
    yield baseline
    if UPDATE:
        AXE_BASELINE.parent.mkdir(parents=True, exist_ok=True)
        AXE_BASELINE.write_text(json.dumps(dict(sorted(baseline.items())), indent=2) + "\n")


@pytest.mark.parametrize("view,viewport", CASES, ids=[case_id(v, p) for v, p in CASES])
def test_view(open_view, axe_baseline, view, viewport):
    page = open_view(view, viewport)
    case = case_id(view, viewport)
    budget_measure(page, view, case)
    aria_check(page, case)
    screenshot_check(page, case)  # before axe, which injects a script tag
    axe_check(page, case, axe_baseline)


@pytest.mark.parametrize("view", list(VIEWS))
def test_view_light_theme_axe(open_view, axe_baseline, view):
    """Colour contrast also holds in the light theme (desktop only)."""
    page = open_view(view, "desktop", scheme="light")
    axe_check(page, f"{view}@desktop-light", axe_baseline)
