"""Every studio view at desktop, tablet and phone sizes: accessibility-tree snapshots, a few
screenshots, an axe-core pass against a baseline, and the chrome budget (report only).

Run: tools/jelli-art/ui_tests/run.sh   (JELLI_UI_UPDATE=1 rewrites the baselines)
"""
import difflib
import json

import pytest
from PIL import Image, ImageChops

from conftest import (BASELINES, BUDGET, CASES, FIXTURE, HERE, NOTES, OUT, SCREENSHOTS, UPDATE, VIEWS, VOLATILE,
                      case_id)

# Landmarks whose accessibility tree is snapshotted (the header holds checkout-dependent items).
# The mode tabs and the asset list are snapshotted once per view, at desktop size; main at every size.
REGIONS = {"modes": "#shell-modes", "assets": "aside", "main": "main"}
PER_VIEW = {"modes", "assets"}
AXE = HERE / "vendor/axe-core/axe.min.js"
AXE_BASELINE = BASELINES / "axe.json"
GATED = {"serious", "critical"}
# A channel difference above TOLERANCE counts as a changed pixel; more than MAX_CHANGED of them fails.
TOLERANCE, MAX_CHANGED = 24, 0.002


def aria_text(page, selector):
    region = page.locator(selector).first
    return region.aria_snapshot() + "\n" if region.is_visible() else "# not visible\n"


def aria_check(page, view, viewport):
    """The region's accessibility tree must equal its baseline exactly."""
    for name, selector in REGIONS.items():
        if name in PER_VIEW and viewport != "desktop":
            continue
        path = BASELINES / "aria" / (f"{view}.{name}.yml" if name in PER_VIEW else f"{case_id(view, viewport)}.{name}.yml")
        actual = aria_text(page, selector)
        if UPDATE:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(actual)
            continue
        assert path.exists(), f"no aria baseline {path.relative_to(HERE)}; run with JELLI_UI_UPDATE=1"
        expected = path.read_text()
        if actual != expected:
            diff = "".join(difflib.unified_diff(expected.splitlines(True), actual.splitlines(True),
                                                str(path.relative_to(HERE)), "actual"))
            pytest.fail(f"{case_id(view, viewport)} {name}: accessibility tree differs from the baseline\n{diff}",
                        pytrace=False)


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
    """Serious and critical violations, as {rule: [node selectors]}, must stay within the baseline."""
    page.add_script_tag(path=str(AXE))
    violations = page.evaluate("""async () => (await axe.run(document, {resultTypes: ['violations']})).violations
        .map(v => ({id: v.id, impact: v.impact, help: v.help, targets: v.nodes.map(n => n.target.join(' '))}))""")
    report = OUT / "axe" / f"{case}.json"
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(violations, indent=2) + "\n")
    gated = {v["id"]: sorted(set(v["targets"])) for v in violations if v["impact"] in GATED}
    if UPDATE:
        baseline[case] = dict(sorted(gated.items()))
        return
    known = {rule: set(targets) for rule, targets in baseline.get(case, {}).items()}
    new = [f"{rule}: {target}" for rule, targets in gated.items() for target in targets
           if target not in known.get(rule, set())]
    gone = sum(len(targets - set(gated.get(rule, []))) for rule, targets in known.items())
    if gone:
        NOTES.append(f"axe {case}: {gone} baseline violation(s) fixed; lower the baseline with JELLI_UI_UPDATE=1")
    assert not new, f"{case}: new serious/critical axe violations (see {report}):\n  " + "\n  ".join(new)


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
    aria_check(page, view, viewport)
    screenshot_check(page, case)  # before axe, which injects a script tag
    axe_check(page, case, axe_baseline)


@pytest.mark.parametrize("view", list(VIEWS))
def test_view_light_theme_axe(open_view, axe_baseline, view):
    """Colour contrast also holds in the light theme (desktop only)."""
    page = open_view(view, "desktop", scheme="light")
    axe_check(page, f"{view}@desktop-light", axe_baseline)
