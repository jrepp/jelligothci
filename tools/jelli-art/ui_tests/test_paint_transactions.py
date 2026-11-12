"""Paint keeps edits across deferred saves/reloads and scopes document shortcuts."""
import json

from playwright.sync_api import expect

from conftest import new_view


def open_paint(browser, studio):
    context, page = new_view(browser, studio, "paint-icon", "desktop")
    page.evaluate("() => { state.artist = 'Transaction test'; JelliShell.showGuide(false); }")
    return context, page


def rgba(page):
    return page.evaluate("() => Array.from(decoded[state.key].after.data)")


def dot(page, x):
    page.evaluate("() => { state.color = D.palette[0]; state.custom = false; }")
    page.keyboard.press("p")
    canvas = page.locator("#stage .paint-canvas canvas")
    zoom = page.evaluate("() => document.querySelector('#stage .paint-canvas canvas').width / asset().width")
    canvas.click(position={"x": (x + 0.5) * zoom, "y": 0.5 * zoom})


def test_save_acknowledges_only_submitted_pixels_and_keeps_later_draft(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    page.route("**/api/save", lambda route: pending.append(route))
    try:
        dot(page, 0)
        sent_pixels = rgba(page)
        page.locator("#save").click()
        page.wait_for_function("() => Studio.paintSaving(state.key)")
        dot(page, 1)
        later_pixels = rgba(page)
        assert later_pixels != sent_pixels
        page.keyboard.press("ControlOrMeta+s")
        assert len(pending) == 1, "A second Save must not duplicate the in-flight request"
        reply = {"ok": True, "sha": "submitted-sha", "version": page.evaluate("D.version")}
        pending.pop(0).fulfill(content_type="application/json", body=json.dumps(reply))
        page.wait_for_function("() => !Studio.paintSaving(state.key)")
        assert rgba(page) == later_pixels
        assert page.evaluate("() => Studio.paintDirty(state.key)")
        expect(page.locator("#save")).to_be_enabled()
        page.wait_for_function("() => !!JelliDrafts.get('paint', state.key)")
        assert page.evaluate("() => Array.from(JelliLayers.composite(JelliLayers.unpack(JelliDrafts.get('paint', state.key).data), decoded[state.key].after).data)") == later_pixels
        page.locator("#save").click()
        page.wait_for_function("() => Studio.paintSaving(state.key)")
        assert pending[0].request.post_data_json["base"] == "submitted-sha"
        pending.pop(0).fulfill(content_type="application/json", body=json.dumps({**reply, "sha": "later-sha"}))
        page.wait_for_function("() => !Studio.paintSaving(state.key)")
        assert not page.evaluate("() => Studio.paintDirty(state.key)")
        assert page.evaluate("() => JelliDrafts.get('paint', state.key)") is None
    finally:
        context.close()


def test_reload_preserves_stroke_made_after_request_started(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    page.route("**/api/data?**", lambda route: pending.append(route))
    try:
        page.evaluate("() => { window.reloadDone = false; Studio.reload().then(() => { window.reloadDone = true; }); }")
        page.wait_for_timeout(50)
        assert pending
        dot(page, 0)
        edited = rgba(page)
        response = page.request.get(studio + "/api/data")
        pending.pop(0).fulfill(status=200, content_type="application/json", body=response.text())
        page.wait_for_function("() => window.reloadDone")
        assert rgba(page) == edited
        assert page.evaluate("() => Studio.paintDirty(state.key)")
        page.keyboard.press("ControlOrMeta+z")
        assert not page.evaluate("() => Studio.paintDirty(state.key)")
    finally:
        context.close()


def test_poll_does_not_supersede_foreground_reload(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    try:
        page.add_init_script("""const originalInterval = window.setInterval;
          window.setInterval = (fn, delay, ...args) => {
            if (delay === 2000) { window.testPoll = fn; return 0; }
            return originalInterval(fn, delay, ...args);
          };""")
        page.reload()
        page.wait_for_function("() => !!window.testPoll")
        page.route("**/api/data?**", lambda route: pending.append(route))
        with page.expect_request("**/api/data?**"):
            page.evaluate("() => { D.version = 'stale'; window.reloadDone = false;"
                          " Studio.reload().then(() => { window.reloadDone = true; }); }")
        page.evaluate("() => window.testPoll()")
        assert len(pending) == 1, "Polling must leave the foreground reload in charge"
        pending.pop().continue_()
        page.wait_for_function("() => window.reloadDone")
        assert page.evaluate("D.version") != "stale"
    finally:
        context.close()


def test_hidden_paint_shortcuts_do_not_change_reviewed_asset(browser, studio):
    context, page = open_paint(browser, studio)
    saves = []
    page.route("**/api/save", lambda route: (saves.append(route.request.post_data_json), route.abort()))
    try:
        dot(page, 0)
        edited = rgba(page)
        page.evaluate("() => JelliShell.activateMode('review')")
        page.locator("main").focus()
        page.keyboard.press("ControlOrMeta+z")
        assert rgba(page) == edited
        # Invoke the studio handler directly to verify it declines native Save;
        # the shell must never save a hidden paint document from another mode.
        assert not page.evaluate("() => Studio.keydown(new KeyboardEvent('keydown', {key:'s', ctrlKey:true}))")
        assert not saves
        reviews_before = page.evaluate("() => JSON.stringify(reviews)")
        for key in ("s", "a", "b"):
            page.locator("main").dispatch_event("keydown", {"key": key, "ctrlKey": True, "bubbles": True})
        assert page.evaluate("() => state.view") == "detail"
        assert page.evaluate("() => JSON.stringify(reviews)") == reviews_before
    finally:
        context.close()


def test_peek_clears_when_window_blurs_without_keyup(browser, studio):
    context, page = open_paint(browser, studio)
    try:
        page.evaluate("() => JelliShell.activateMode('review')")
        page.locator("main").focus()
        page.keyboard.down("Space")
        assert page.evaluate("() => state.peek && document.body.classList.contains('peeking')")
        page.evaluate("() => window.dispatchEvent(new Event('blur'))")
        assert not page.evaluate("() => state.peek || document.body.classList.contains('peeking')")
        page.keyboard.up("Space")
    finally:
        context.close()


def test_stale_reload_cannot_replace_pixels_acknowledged_by_a_later_save(browser, studio):
    context, page = open_paint(browser, studio)
    loads, saves = [], []
    original = page.request.get(studio + "/api/data").text()
    page.route("**/api/data?**", lambda route: loads.append(route))
    page.route("**/api/save", lambda route: saves.append(route))
    try:
        page.evaluate("() => { window.reloadDone = false; Studio.reload().then(() => { window.reloadDone = true; }); }")
        page.wait_for_timeout(50)
        assert loads
        dot(page, 0)
        submitted = rgba(page)
        page.locator("#save").click()
        page.wait_for_function("() => Studio.paintSaving(state.key)")
        saves.pop(0).fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "sha": "newer-save", "version": page.evaluate("D.version")}))
        page.wait_for_function("() => !Studio.paintSaving(state.key)")
        loads.pop(0).fulfill(status=200, content_type="application/json", body=original)
        page.wait_for_function("() => window.reloadDone")
        assert rgba(page) == submitted
        assert not page.evaluate("() => Studio.paintDirty(state.key)")
        assert page.evaluate("() => byKey[state.key].sha") == "newer-save"
    finally:
        context.close()


def test_activities_save_keeps_recipe_edits_made_during_request(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    page.route("**/api/content", lambda route: pending.append(route))
    try:
        page.evaluate("() => { D.behavior_editable = true; JelliShell.activateMode('activities'); }")
        field = page.locator("#activities input[data-field=duration_s]")
        field.fill("44")
        field.press("Tab")
        page.locator("#ac-save").click()
        expect(page.locator("#ac-save-status")).to_have_attribute("data-save-state", "saving")
        field.fill("55")
        field.press("Tab")
        page.keyboard.press("ControlOrMeta+s")
        assert len(pending) == 1
        assert pending[0].request.post_data_json["docs"]["activities"]["moments"][0]["duration_s"] == 44
        pending.pop(0).fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "changed": ["activities"], "shas": {"activities": "recipe-snapshot"}, "version": page.evaluate("D.version")}))
        expect(page.locator("#ac-save-status")).to_have_attribute("data-save-state", "idle")
        assert field.input_value() == "55"
        assert page.evaluate("() => D.activity_data.moments[0].duration_s") == 44
        assert page.evaluate("() => Studio.activitiesDirty()")
        expect(page.locator("#ac-save")).to_be_enabled()
        page.wait_for_function("() => JelliDrafts.get('activities','activities')?.data.moments[0].duration_s === 55")
    finally:
        context.close()


def test_clip_save_keeps_later_duration_and_failed_retry_is_available(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    page.route("**/api/clips", lambda route: pending.append(route))
    try:
        page.evaluate("() => { state.cForm='axolotl'; state.cPose='idle'; JelliShell.activateMode('creature'); Studio.cr.change(w => {w.durations_ms[0]=111;}); }")
        page.locator("#cr-save").click()
        expect(page.locator("#cr-save-status")).to_have_attribute("data-save-state", "saving")
        page.evaluate("() => Studio.cr.change(w => {w.durations_ms[0]=222;})")
        page.keyboard.press("ControlOrMeta+s")
        assert len(pending) == 1
        assert pending[0].request.post_data_json["clips"][0]["durations_ms"][0] == 111
        pending.pop(0).fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "changed": ["axolotl.idle"], "clip_shas": {"axolotl.idle": "clip-snapshot"}, "version": page.evaluate("D.version")}))
        expect(page.locator("#cr-save-status")).to_have_attribute("data-save-state", "idle")
        assert page.evaluate("() => Studio.cr.current('axolotl.idle').durations_ms[0]") == 222
        assert page.evaluate("() => Studio.cr.clipOf('axolotl.idle').durations_ms[0]") == 111
        page.wait_for_function("() => JelliDrafts.get('clips','clips')?.data.clips['axolotl.idle'].durations_ms[0] === 222")
        page.locator("#cr-save").click()
        expect(page.locator("#cr-save-status")).to_have_attribute("data-save-state", "saving")
        assert pending[0].request.post_data_json["bases"]["axolotl.idle"] == "clip-snapshot"
        pending.pop(0).fulfill(status=500, content_type="application/json", body=json.dumps({"error": "Disk unavailable"}))
        expect(page.locator("#cr-save-status")).to_have_attribute("data-save-state", "error")
        expect(page.locator("#cr-save")).to_be_enabled()
        assert page.evaluate("() => Studio.cr.current('axolotl.idle').durations_ms[0]") == 222
    finally:
        context.close()


def test_size_save_acknowledges_snapshot_and_retains_later_profile(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    page.route("**/api/creatures", lambda route: pending.append(route))
    try:
        page.evaluate("() => { D.creature_data_editable=true; JelliShell.activateMode('creature'); Studio.creatureDoc.work.profiles[0].scale=4; Studio.creatureDoc.refresh(); }")
        page.locator("#bh-save").click()
        expect(page.locator("#bh-save-status")).to_have_attribute("data-save-state", "saving")
        page.evaluate("() => { Studio.creatureDoc.work.profiles[0].scale=5; Studio.creatureDoc.refresh(); }")
        assert pending[0].request.post_data_json["data"]["profiles"][0]["scale"] == 4
        pending.pop(0).fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "sha": "profile-snapshot", "version": page.evaluate("D.version")}))
        expect(page.locator("#bh-save-status")).to_have_attribute("data-save-state", "idle")
        assert page.evaluate("() => Studio.creatureDoc.work.profiles[0].scale") == 5
        assert page.evaluate("() => D.creature_data.profiles[0].scale") == 4
        expect(page.locator("#bh-save")).to_be_enabled()
        page.wait_for_function("() => JelliDrafts.get('creatures','creatures')?.data.profiles[0].scale === 5")
    finally:
        context.close()


def test_behaviour_save_retains_state_edits_made_during_request(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    page.route("**/api/content", lambda route: pending.append(route))
    try:
        page.evaluate("() => { D.behavior_editable=true; JelliShell.activateMode('behaviour'); }")
        field = page.locator('[data-b="states.0.cooldown_s"]')
        field.fill("77")
        field.press("Tab")
        page.locator("#bv-save").click()
        expect(page.locator("#bv-save-status")).to_have_attribute("data-save-state", "saving")
        field.fill("78")
        field.press("Tab")
        assert pending[0].request.post_data_json["docs"]["behaviors"]["states"][0]["cooldown_s"] == 77
        pending.pop(0).fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "changed": ["behaviors"], "shas": {"behaviors": "state-snapshot"}, "version": page.evaluate("D.version")}))
        expect(page.locator("#bv-save-status")).to_have_attribute("data-save-state", "idle")
        assert field.input_value() == "78"
        assert page.evaluate("() => D.behavior_data.states[0].cooldown_s") == 77
        expect(page.locator("#bv-save")).to_be_enabled()
        page.wait_for_function("() => JelliDrafts.get('behaviour','content')?.data.behaviors.states[0].cooldown_s === 78")
    finally:
        context.close()


def test_deferred_frame_copy_updates_original_dirty_clip_after_selection_changes(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    payload = page.request.get(studio + "/api/data").json()
    page.route("**/api/frames", lambda route: pending.append(route) if route.request.method == "POST" else route.continue_())
    try:
        page.evaluate("() => { state.cForm='axolotl'; state.cPose='idle'; JelliShell.activateMode('creature'); Studio.cr.change(w => {w.durations_ms[0]=111;}); }")
        target = "axolotl.idle"
        page.locator("#an-dup").click()
        page.wait_for_timeout(50)
        assert pending
        source = pending[0].request.post_data_json["from"]
        assert "clip" not in pending[0].request.post_data_json, "Dirty clip insertion must stay in the working document"
        other = page.evaluate("() => D.clips.find(c => !c.key.startsWith('axolotl.') && c.frames.length).key")
        other_before = page.evaluate("key => JSON.stringify(Studio.cr.current(key))", other)
        page.evaluate("key => { [state.cForm,state.cPose]=key.split('.'); Studio.cr.rerender(); }", other)
        copied = dict(next(asset for asset in payload["assets"] if asset["key"] == source))
        copied.update(key="creatures.axolotl-deferred-copy", id=1990)
        payload["assets"].append(copied)
        page.route("**/api/data?**", lambda route: route.fulfill(content_type="application/json", body=json.dumps(payload)))
        pending.pop(0).fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "key": copied["key"], "id": copied["id"], "version": payload["version"]}))
        page.wait_for_function("([target, frame]) => Studio.cr.current(target).frames.includes(frame)", arg=[target, copied["key"]])
        assert page.evaluate("() => Studio.cr.selectedKey()") == other
        assert page.evaluate("key => JSON.stringify(Studio.cr.current(key))", other) == other_before
        assert page.evaluate("key => Studio.cr.current(key).durations_ms[0]", target) == 111
        assert page.evaluate("key => Studio.cr.current(key).frames.every(frame => byKey[frame]?.form === key.split('.')[0])", target)
    finally:
        context.close()


def test_paint_copy_merges_later_clip_edit_without_stealing_the_active_document(browser, studio):
    context, page = open_paint(browser, studio)
    pending = []
    payload = page.request.get(studio + "/api/data").json()
    page.route("**/api/frames", lambda route: pending.append(route) if route.request.method == "POST" else route.continue_())
    target = "axolotl.idle"
    try:
        source = page.evaluate("key => { state.cForm='axolotl';state.cPose='idle';const frame=Studio.cr.current(key).frames[0];select(frame);JelliShell.activateMode('paint');return frame; }", target)
        page.evaluate("() => { JelliAnimation.duplicatePaintFrame(); }")
        page.wait_for_timeout(50)
        assert pending
        request = pending[0].request.post_data_json
        assert request["clip"] == target
        page.evaluate("() => { JelliShell.activateMode('creature'); Studio.cr.change(w=>{w.durations_ms[0]=111;}); JelliShell.activateMode('review');select('icons.food'); }")
        copied = dict(next(asset for asset in payload["assets"] if asset["key"] == source))
        copied.update(key="creatures.axolotl-paint-deferred-copy", id=1991)
        payload["assets"].append(copied)
        saved_clip = next(clip for clip in payload["clips"] if clip["key"] == target)
        saved_clip["frames"].insert(request["index"], copied["key"])
        saved_clip["durations_ms"].insert(request["index"], request["duration_ms"])
        payload["clip_shas"][target] = "inserted-clip"
        page.route("**/api/data?**", lambda route: route.fulfill(content_type="application/json", body=json.dumps(payload)))
        pending.pop(0).fulfill(content_type="application/json", body=json.dumps(
            {"ok": True, "key": copied["key"], "id": copied["id"], "clip": target, "version": payload["version"]}))
        page.wait_for_function("([target, frame]) => Studio.cr.current(target).frames.includes(frame)", arg=[target, copied["key"]])
        assert page.evaluate("key => Studio.cr.current(key).durations_ms[0]", target) == 111
        assert page.evaluate("([key,frame]) => Studio.cr.current(key).frames.filter(f=>f===frame).length", [target, copied["key"]]) == 1
        assert page.evaluate("() => [state.view,state.mode,state.key]") == ["detail", "side", "icons.food"]
        assert page.evaluate("key => Studio.cr.clipDirty(key)", target)
    finally:
        context.close()
