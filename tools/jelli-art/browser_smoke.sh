#!/bin/sh
# Optional browser smoke test for Jelli Art (needs agent-browser on PATH).
#
#   tools/jelli-art/browser_smoke.sh [port]
#
# Serves scratch copies of assets/slice and content/, then in a real browser:
# paints one stroke, checks the draft survives a reload, saves (the draft is
# dropped and the PNG changes on disk), and checks a save over a file changed
# behind the page is refused rather than overwriting it. The checkout is never
# written. test_server.py covers the same server paths without a browser.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
port="${1:-18965}"
url="http://127.0.0.1:$port"
session="jelli-art-smoke-$port"
# Something already answering on the port would take this test's paint and saves into its own files.
if curl -s -o /dev/null --max-time 2 "$url/" 2>/dev/null; then  # any HTTP answer, not only a studio
    echo "FAIL: $url already answers; stop that server or pass a free port" >&2
    exit 1
fi
work=$(mktemp -d "${TMPDIR:-/tmp}/jelli-art-smoke.XXXXXX")
cp -R "$repo/assets/slice" "$work/slice"
cp -R "$repo/content" "$work/content"

"$repo/scripts/uv" run --python 3.12 "$here/jelli_art.py" --assets "$work/slice" --content "$work/content" \
    --no-open --port "$port" >"$work/server.log" 2>&1 &
server=$!
cleanup() {
    agent-browser --session "$session" close >/dev/null 2>&1 || true
    kill "$server" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT INT TERM

ab() { agent-browser --session "$session" "$@"; }
fail() { echo "FAIL: $*" >&2; tail -20 "$work/server.log" >&2; exit 1; }
js() { ab eval "$1" | tail -1 | tr -d '"'; }

tries=0
until curl -fsS "$url/healthz" >/dev/null 2>&1; do
    kill -0 "$server" 2>/dev/null || fail "server exited before answering"
    tries=$((tries + 1))
    [ "$tries" -lt 100 ] || fail "server did not start"
    sleep 0.2
done

key=creatures.baby-idle-a
png="$work/slice/creatures/baby-idle-a.png"
ab open "$url/#key=$key&mode=paint" >/dev/null 2>&1 || fail "agent-browser could not open the page"  # the daemon keeps inherited fds
ab wait --fn "typeof D !== 'undefined' && D.assets && D.assets.length > 0 && window.JelliDrafts && state.mode === 'paint'" >/dev/null
ab eval "localStorage.clear()" >/dev/null

# One pencil stroke across the middle of the canvas, in a colour the sprite does not use.
ab eval "state.custom = true; state.color = '#00ff7f'" >/dev/null  # off-palette needs custom
# The paint canvas's centre in the viewport. Scroll it into view first: the shell's
# first-run guide (and narrow windows) can push it below the fold.
centre() {
    js "(() => { const c = document.querySelector('canvas[style*=crosshair]'); c.scrollIntoView({block: 'center'}); const r = c.getBoundingClientRect(); return Math.round(r.x + r.width / 2) + ' ' + Math.round(r.y + r.height / 2); })()" | tr -d '\r'
}
centre=$(centre)
# shellcheck disable=SC2086 # split "x y" into two arguments
set -- $centre
ab mouse move "$1" "$2" >/dev/null
ab mouse down >/dev/null
ab mouse move "$(($1 + 30))" "$2" >/dev/null
ab mouse up >/dev/null
ab wait --fn "JelliDrafts.list('paint').length === 1" >/dev/null || fail "no draft after painting"
echo "ok: painting stores a draft"

ab reload >/dev/null
ab wait --fn "window.Studio && Studio.paintDirty && Studio.paintDirty('$key')" >/dev/null || fail "draft not restored after reload"
ab wait --text "Restored unsaved edits" >/dev/null || fail "no restore notice"
echo "ok: the draft comes back after a reload"

before=$(shasum "$png")
# The restored draft enables Save a moment after the restore notice; wait for it.
ab wait --fn "!document.getElementById('save').disabled" >/dev/null || fail "Save stayed disabled after the restore"
ab eval "document.getElementById('save').click()" >/dev/null
ab wait --fn "JelliDrafts.list('paint').length === 0 && !Studio.paintDirty('$key')" >/dev/null || fail "save did not clear the draft"
[ "$(shasum "$png")" != "$before" ] || fail "save did not change the PNG"
echo "ok: saving writes the PNG and drops the draft"

# Change the file behind the page, then save an edit made from the old version.
ab eval "state.custom = true; state.color = '#7f00ff'" >/dev/null
centre=$(centre)  # The reload reset the scroll position.
# shellcheck disable=SC2086 # split "x y" into two arguments
set -- $centre
ab mouse move "$(($1 - 30))" "$(($2 + 40))" >/dev/null
ab mouse down >/dev/null
ab mouse up >/dev/null
ab wait --fn "Studio.paintDirty('$key')" >/dev/null || fail "the second stroke did not register"
"$repo/scripts/uv" run --python 3.12 --with Pillow==12.0.0 python -c "
from PIL import Image; import sys
im = Image.open(sys.argv[1]).convert('RGBA'); w, h = im.size
x, y = next((x, y) for y in range(h) for x in range(w) if im.getpixel((x, y))[3])
im.putpixel((x, y), (1, 2, 3, 255)); im.save(sys.argv[1])" "$png"
changed=$(shasum "$png")
# The studio asks through the page shell's <dialog> (JelliShell.confirm), or a native
# confirm() when the shell is absent; answer whichever appears.
answer() {
    # The save's stale check is a round trip, so wait for the question rather than racing it.
    ab wait --fn "!!document.querySelector('dialog[open]')" >/dev/null 2>&1 || true
    if [ "$(js "!!document.querySelector('dialog[open] [value=$1]')")" = true ]; then
        ab eval "document.querySelector('dialog[open] [value=$1]').click()" >/dev/null
    elif [ "$1" = ok ]; then ab dialog accept >/dev/null; else ab dialog dismiss >/dev/null; fi
}
click_save() {
    ab wait --fn "!document.getElementById('save').disabled && !document.querySelector('dialog[open]')" >/dev/null || fail "Save is not ready"
    ab eval "setTimeout(() => document.getElementById('save').click(), 0)" >/dev/null
}
click_save
answer cancel || fail "no overwrite question for a file changed on disk"
ab wait --text "changed on disk" >/dev/null || fail "stale save was not reported"
[ "$(shasum "$png")" = "$changed" ] || fail "stale save overwrote the newer file"
echo "ok: a save over a file changed on disk asks first; Cancel keeps the newer file"
click_save
answer ok || fail "no overwrite question the second time"
ab wait --fn "!Studio.paintDirty('$key')" >/dev/null || fail "accepted overwrite did not save"
[ "$(shasum "$png")" != "$changed" ] || fail "accepted overwrite did not write the PNG"
echo "ok: OK overwrites it"

errors=$(ab errors 2>&1 || true)
case "$errors" in
    *rror*) fail "page errors: $errors" ;;
esac
echo "browser smoke passed"
