/* Jelli Art: lint actions in the studio. Loaded after studio.js.
 * - Waive… beside the lint chips: waive failing rules for one sprite with a reason (POST /api/lint-waiver,
 *   stored in assets/slice/source/lint.json), or remove the waiver.
 * - Tidy outline preview: before, after and the changed pixels, applied only on Apply.
 * - A once-per-session note when Flip horizontal moves a shaded sprite's light to the top right. */
'use strict';
(() => {
  const S = window.Studio, L = window.JelliLint, shell = () => window.JelliShell;
  if (!S || !L) return;
  const LOCKED = new Set(['font', 'backgrounds']);
  const esc = t => String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
  document.head.insertAdjacentHTML('beforeend', `<style>
    .lint-waive{font-size:11px;padding:2px 9px;border-radius:20px}
    .lint-form{display:grid;gap:10px;min-width:min(420px,80vw)}.lint-form label{display:flex;gap:8px;align-items:center}
    .lint-form input[type=text]{font:inherit;color:var(--ink);background:var(--raised);border:1px solid var(--line);border-radius:6px;padding:6px 8px}
    .tidy-preview{display:flex;gap:14px;flex-wrap:wrap}.tidy-preview figure{margin:0;display:grid;gap:4px;justify-items:center;font-size:12px;color:var(--muted)}
    .tidy-preview canvas{image-rendering:pixelated;border:1px solid var(--line);border-radius:6px}
  </style>`);

  /* ---------- waivers ---------- */
  S.afterHeader = (a, stats, v) => {
    if (!D.live || LOCKED.has(a.kind)) return;
    const waiver = D.lint?.waivers?.[a.key];
    if (!v.fails.length && !waiver) return;
    const b = document.createElement('button'); b.className = 'lint-waive';
    b.textContent = waiver ? 'Edit waiver…' : 'Waive…';
    b.title = 'Accept an intentional lint failure for this sprite, with a reason (saved in assets/slice/source/lint.json)';
    b.onclick = () => editWaiver(a);
    stats.append(b);
  };
  async function editWaiver(a) {
    const v = L.verdict(a.key, a.kind, a.after_metrics, {...D.lint, waivers: {}}), waiver = D.lint?.waivers?.[a.key];
    const rules = L.RULES.filter(r => v.fails.includes(r) || waiver?.rules.includes(r));
    let form = null;
    const body = `<div class="lint-form"><p>Waived rules stop counting as failures for <b>${esc(a.key)}</b>. Say why, so a reviewer can judge it.</p>
      ${rules.map(r => `<label><input type="checkbox" value="${r}"${waiver ? (waiver.rules.includes(r) ? ' checked' : '') : ' checked'}> ${L.NAMES[r]}${v.fails.includes(r) ? '' : ' (passes now)'}</label>`).join('')}
      <label for="lint-reason">Reason</label><input type="text" id="lint-reason" maxlength="200" value="${esc(waiver?.reason || '')}" placeholder="e.g. selective outline is intentional on the gills"></div>`;
    const actions = [{label: 'Cancel', value: 'cancel'}, ...(waiver ? [{label: 'Remove waiver', value: 'remove', danger: true}] : []), {label: 'Save waiver', value: 'save', primary: true}];
    const choice = await shell().dialog({title: 'Lint waiver', body, actions, initial: '#lint-reason', onOpen: el => {
      form = el;  // Enter in the reason saves (the form's first button is the dialog's close ×)
      el.querySelector('#lint-reason').addEventListener('keydown', e => { if (e.key === 'Enter') { e.preventDefault(); el.querySelector('[value=save]').click(); } });
    }});
    if (choice !== 'save' && choice !== 'remove') return;
    const picked = choice === 'remove' ? [] : [...form.querySelectorAll('input[type=checkbox]:checked')].map(i => i.value);
    const reason = form.querySelector('#lint-reason').value.trim();
    if (choice === 'save' && (!picked.length || !reason)) return S.status('Pick at least one rule and give a reason to waive it', 'warn');
    try {
      const res = await S.api('POST', '/api/lint-waiver', {key: a.key, rules: picked, reason, base: D.lint_sha, artist: state.artist});
      Object.assign(D, {lint: res.lint, lint_sha: res.lint_sha, version: res.version});
      S.rerender();
      S.status(picked.length ? `Waived ${picked.map(r => L.NAMES[r]).join(', ')} for ${a.key}` : `Removed the waiver for ${a.key}`, 'ok');
    } catch (err) {
      if (err.status === 409) { await S.reload(); S.rerender(); }
      S.status(`Waiver not saved: ${err.message}`, 'bad', true);
    }
  }

  /* ---------- Tidy outline preview ---------- */
  function sheet(pixels, w, h, z, mark) {
    const c = document.createElement('canvas'); c.width = w * z; c.height = h * z;
    const ctx = c.getContext('2d');
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      ctx.fillStyle = (x + y) % 2 ? '#3a3942' : '#2b2b31'; ctx.fillRect(x * z, y * z, z, z);
      const hex = pixels[y * w + x]; if (hex) { ctx.fillStyle = hex; ctx.fillRect(x * z, y * z, z, z); }
    }
    if (mark) { ctx.strokeStyle = '#ff4dd8'; ctx.lineWidth = 2; for (const i of mark) ctx.strokeRect((i % w) * z + 1, Math.floor(i / w) * z + 1, z - 2, z - 2); }
    return c;
  }
  /* Resolves true when the artist applies the tidied pixels. */
  S.previewTidy = async (a, before, after) => {
    const changed = after.flatMap((hex, i) => hex === before[i] ? [] : [i]);
    if (!changed.length) { S.status('Tidy outline found nothing to change'); return false; }
    const z = Math.max(3, Math.min(8, Math.floor(200 / Math.max(a.width, a.height)))), box = document.createElement('div');
    box.innerHTML = `<p>Tidy outline would change <b>${changed.length}</b> pixel${changed.length === 1 ? '' : 's'}: a closed outline, specks removed and the bottom shadow. Changed pixels are outlined in pink.</p><div class="tidy-preview"></div>`;
    const row = box.querySelector('.tidy-preview');
    for (const [caption, pixels, mark] of [['now', before, null], ['tidied', after, null], ['changes', after, changed]]) {
      const fig = document.createElement('figure'), c = sheet(pixels, a.width, a.height, z, mark);
      c.setAttribute('role', 'img'); c.setAttribute('aria-label', `${a.key} ${caption}`);
      fig.append(c); fig.insertAdjacentHTML('beforeend', `<figcaption>${caption}</figcaption>`); row.append(fig);
    }
    const choice = await shell().dialog({title: 'Tidy outline', body: box, actions: [{label: 'Cancel', value: 'cancel'}, {label: 'Apply', value: 'apply', primary: true}]});
    return choice === 'apply';
  };

  /* ---------- Flip horizontal and the top-left light ---------- */
  let flipWarned = false;
  S.warnFlip = a => {
    if (flipWarned || !a || LOCKED.has(a.kind)) return;
    if (Object.keys(a.after_metrics.colors).length < 4) return;  // flat art: no shading to move
    flipWarned = true;
    shell()?.notify('Flipped: the shading now reads as light from the top right. The house light is top left, so re-shade it or flip back if that matters here.',
      {tone: 'warn', id: 'flip-light'});
  };
})();
