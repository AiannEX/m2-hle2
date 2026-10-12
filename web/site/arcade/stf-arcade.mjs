// The arcade, on the page that runs the game (m2-hle2's web build at play.sonicthefighte.rs).
//
//   <meta name="stf-arcade-api" content="https://arcade.sonicthefighte.rs">
//   <script type="module" src="arcade/stf-arcade.mjs"></script>
//
// The page ships its own copy of this file, ranking.mjs and stf-arcade.css (m2-hle2's
// web/site/arcade/: its build refuses scripts from other sites); only the API calls go to
// the arcade. Without the meta tag the API is wherever this module came from.
//
// Asks once, plainly, whether the player wants to share, and keeps the answer in a cookie.
// Two separate choices, each can be turned off at any time from the "Arcade & your data"
// button:
//   arcade  the game boots on the shared board (the Fly's backup RAM: its ranking, its
//           settings), and new ranking entries (initials + clear time) go back to it.
//   usage   a few anonymous events: page opened, game started (and how), time played.
// Saying no to both keeps the game as it was: its own board, kept in this browser.
//
// The page calls, if this module is there:
//   await window.stfArcade.ready    before web_rom_load (the board must be in localStorage)
//   window.stfArcade.onGame(mode)   once the game runs: 'solo' 'netplay' 'ggpo' 'follow'

import { NVRAM_KEY, BACKUP_SIZE, hexToBytes, readRanking, newEntries } from './ranking.mjs';

const API = (document.querySelector('meta[name=stf-arcade-api]') || {}).content ||
  new URL('.', import.meta.url).origin;
const LOCAL_KEY = NVRAM_KEY + '.local';      // the player's own board, kept while they play ours
const SENT_KEY = 'stfArcade.sent';           // entries the shared board already has
const YEAR = 365 * 24 * 3600;

/* ---- The choice, in cookies ------------------------------------------------- */

function cookie(name) {
  const m = document.cookie.match(new RegExp('(?:^|; )' + name + '=([^;]*)'));
  return m ? decodeURIComponent(m[1]) : null;
}

function setCookie(name, value, maxAge = YEAR) {
  document.cookie = `${name}=${encodeURIComponent(value)}; Max-Age=${maxAge}; Path=/; SameSite=Lax; Secure`;
}

/* { arcade, usage } once they chose, null before. */
export function readConsent() {
  const v = cookie('stf_consent');
  if (v === null) return null;
  const on = v.split('.');
  return { arcade: on.includes('arcade'), usage: on.includes('usage') };
}

function newPlayerId() {
  const b = crypto.getRandomValues(new Uint8Array(16));
  return Array.from(b, (x) => (x | 256).toString(16).slice(1)).join('');
}

function player() { return cookie('stf_player'); }

function saveConsent(c) {
  const on = ['arcade', 'usage'].filter((k) => c[k]);
  setCookie('stf_consent', on.length ? on.join('.') : 'none');
  if (on.length && !player()) setCookie('stf_player', newPlayerId());
  if (!on.length) setCookie('stf_player', '', 0);
}

/* ---- Talking to the arcade ------------------------------------------------- */

/* text/plain keeps these "simple" requests: no preflight, and sendBeacon can carry them. */
function post(path, body, { beacon = false } = {}) {
  const text = JSON.stringify(body);
  if (beacon && navigator.sendBeacon) {
    navigator.sendBeacon(API + path, new Blob([text], { type: 'text/plain' }));
    return Promise.resolve(null);
  }
  return fetch(API + path, { method: 'POST', body: text, headers: { 'Content-Type': 'text/plain' },
    keepalive: true }).then((r) => (r.ok && r.status !== 204 ? r.json() : null));
}

function usage(event, data = {}, opts) {
  const c = readConsent();
  if (c && c.usage && player()) post('/api/usage', { player: player(), event, data }, opts).catch(() => {});
}

/* ---- The board ----------------------------------------------------------- */

function localBoard() {
  const bytes = hexToBytes(localStorage.getItem(NVRAM_KEY) || '');
  return bytes && bytes.length === BACKUP_SIZE ? bytes : null;
}

function sent() { try { return JSON.parse(localStorage.getItem(SENT_KEY)) || []; } catch { return []; } }

/* Puts the shared board where the emulator will read it, keeping the player's own aside. */
async function joinBoard() {
  const r = await fetch(API + '/api/board', { signal: AbortSignal.timeout(5000) });   // never hold the game up long
  if (!r.ok) throw new Error('board: ' + r.status);
  const { hex } = await r.json();
  const mine = localStorage.getItem(NVRAM_KEY);
  if (mine !== null && localStorage.getItem(LOCAL_KEY) === null) localStorage.setItem(LOCAL_KEY, mine);
  localStorage.setItem(NVRAM_KEY, hex);
  localStorage.setItem(SENT_KEY, JSON.stringify(readRanking(hexToBytes(hex)).map((e) => e.hex)));
}

/* Gives the player their own board back (from the next time the game starts). */
function leaveBoard() {
  const mine = localStorage.getItem(LOCAL_KEY);
  if (mine !== null) localStorage.setItem(NVRAM_KEY, mine);
  else localStorage.removeItem(NVRAM_KEY);
  localStorage.removeItem(LOCAL_KEY);
  localStorage.removeItem(SENT_KEY);
}

function joined() { return localStorage.getItem(SENT_KEY) !== null; }

/* Sends what the player put on the ranking since the board loaded. */
function syncScores(opts) {
  const c = readConsent();
  const board = c && c.arcade && joined() && localBoard();
  if (!board || !player()) return;
  const fresh = newEntries(sent().map((hex) => ({ hex })), readRanking(board));
  if (!fresh.length) return;
  localStorage.setItem(SENT_KEY, JSON.stringify(sent().concat(fresh.map((e) => e.hex))));
  post('/api/scores', { player: player(), entries: fresh.map((e) => e.hex) }, opts).catch(() => {});
}

/* ---- What the page sees -------------------------------------------------- */

let started = 0;

async function prepare() {
  const c = readConsent();
  if (c && c.arcade) {
    try { await joinBoard(); } catch (e) { console.warn('arcade: playing on your own board,', e.message); }
  } else if (joined()) leaveBoard();
}

function onGame(mode = 'solo') {
  started = Date.now();
  usage('game_start', { mode });
  setInterval(syncScores, 60000);        // the emulator keeps its backup RAM once a minute too
}

addEventListener('pagehide', () => {
  if (!started) return;
  if (window.Module && Module._web_backup_flush) Module._web_backup_flush();
  syncScores({ beacon: true });
  usage('play_time', { seconds: (Date.now() - started) / 1000 }, { beacon: true });
});

/* ---- The banner and the panel -------------------------------------------- */

const WHAT = `
<p><b>Arcade: shared high scores.</b> Your game starts on the same board as everyone
else's, the one the Fly plays on the stream, and fights for the same ranking. When you set
a time, its <b>initials and clear time</b> are sent to the arcade and shown to everyone.</p>
<p><b>Usage stats.</b> Anonymous counts that help us keep the site working: that the page
opened, that a game started (alone, netplay or watching), and how long it ran.
Nothing else: no address, no browser details, no tracking across sites.</p>
<p>Either one gives this browser a random id (cookie <code>stf_player</code>) so you can
see or delete what was sent. Your choice is kept in the cookie <code>stf_consent</code>.
<a href="${API}/privacy.html" target="_blank" rel="noopener">The details</a>.</p>`;

function el(html) {
  const t = document.createElement('template');
  t.innerHTML = html.trim();
  return t.content.firstChild;
}

function choose(c, box) {
  const before = readConsent();
  saveConsent(c);
  box.remove();
  if (!!(before && before.arcade) === c.arcade) return;
  if (started) note('Your choice applies the next time the game starts. Reload the page to switch boards now.');
  else ready = ready.then(prepare);       // the game has not loaded yet: switch boards now
}

function note(text) {
  const n = el(`<div class="stfa-note" role="status">${text}</div>`);
  document.body.append(n);
  setTimeout(() => n.remove(), 8000);
}

function banner() {
  const box = el(`<div class="stfa-banner" role="dialog" aria-label="Join the arcade?">
    <h2>Join the arcade?</h2>${WHAT}
    <div class="stfa-buttons">
      <button data-a="all">Join, share both</button>
      <button data-a="arcade">High scores only</button>
      <button data-a="none">No thanks, play alone</button>
    </div></div>`);
  box.addEventListener('click', (e) => {
    const a = e.target.dataset && e.target.dataset.a;
    if (a) choose({ arcade: a !== 'none', usage: a === 'all' }, box);
  });
  document.body.append(box);
}

function panelRows(c) {
  return `<label><input type="checkbox" name="arcade" ${c.arcade ? 'checked' : ''}>
      Arcade: share my high scores, play on the shared board</label>
    <label><input type="checkbox" name="usage" ${c.usage ? 'checked' : ''}>
      Usage stats: anonymous counts of visits, games and play time</label>`;
}

function panel() {
  const c = readConsent() || { arcade: false, usage: false };
  const box = el(`<div class="stfa-banner" role="dialog" aria-label="Arcade and your data">
    <h2>Arcade &amp; your data</h2>${WHAT}${panelRows(c)}
    <pre class="stfa-mine" hidden></pre>
    <div class="stfa-buttons">
      <button data-a="save">Save</button>
      <button data-a="show">What have you got on me?</button>
      <button data-a="forget">Delete what I sent</button>
      <button data-a="close">Close</button>
    </div></div>`);
  box.addEventListener('click', (e) => panelAction(e.target.dataset && e.target.dataset.a, box));
  document.body.append(box);
}

async function panelAction(a, box) {
  const pre = box.querySelector('.stfa-mine');
  if (a === 'save') {
    choose({ arcade: box.querySelector('[name=arcade]').checked,
      usage: box.querySelector('[name=usage]').checked }, box);
  } else if (a === 'close') box.remove();
  else if (a === 'show' || a === 'forget') {
    pre.hidden = false;
    if (!player()) { pre.textContent = 'Nothing: this browser has not sent anything.'; return; }
    const out = await post(a === 'show' ? '/api/me' : '/api/forget', { player: player() }).catch((e) => e.message);
    pre.textContent = JSON.stringify(out, null, 1);
    if (a === 'forget') { setCookie('stf_player', '', 0); saveConsent(readConsent() || {}); }
  }
}

function start() {
  const css = el(`<link rel="stylesheet" href="${new URL('stf-arcade.css', import.meta.url)}">`);
  document.head.append(css);
  const button = el('<button class="stfa-open" title="Arcade &amp; your data">Arcade &amp; your data</button>');
  button.addEventListener('click', panel);
  document.body.append(button);
  if (!readConsent()) banner();
}

let ready = prepare().then(() => usage('visit'));
window.stfArcade = { get ready() { return ready; }, onGame, readConsent };
if (document.body) start(); else addEventListener('DOMContentLoaded', start);
