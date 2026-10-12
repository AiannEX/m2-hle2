/*
 * Keeping a game's save in this browser, for a game the arcade does not serve
 * (Sega Rally: srally.sonicthefighte.rs, ?game=srallyc).
 *
 * Sonic the Fighters has the arcade (arcade/stf-arcade.mjs): the Fly's shared
 * board and its ranking. The Fly plays nothing else, so another game gets no
 * shared board; it asks instead whether to keep the player's own board, its
 * backup RAM (settings and best times), in localStorage from one visit to the
 * next. Nothing is kept until they say yes. The answer is kept in localStorage
 * (m2hle2.keep.<game>), and the "Your save" button changes it at any time:
 * turning it off deletes the kept save.
 *
 * The page calls m2hleSave.beforeLoad() just before web_rom_load.
 */
'use strict';

const m2hleSave = (() => {
  const game = m2hleGame.id;
  if (game === 'sfight') return null;
  const CHOICE_KEY = 'm2hle2.keep.' + game;
  const NVRAM_KEY = 'm2hle2.nvram.' + game;     /* backup_ram.h: m2hle2.nvram.<set> */
  let loaded = false;

  function get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } }
  function set(k, v) { try { localStorage.setItem(k, v); } catch (e) {} }
  function drop(k) { try { localStorage.removeItem(k); } catch (e) {} }

  function choice() { const v = get(CHOICE_KEY); return v === null ? null : v === '1'; }
  function keep() { return choice() === true; }

  function apply(on) {
    set(CHOICE_KEY, on ? '1' : '0');
    if (!on) drop(NVRAM_KEY);
    /* The game running now follows at once: on stores what it holds, off stops writing. */
    if (loaded && window.Module && Module._web_backup_keep) Module._web_backup_keep(on ? 1 : 0);
  }

  function el(html) {
    const t = document.createElement('template');
    t.innerHTML = html.trim();
    return t.content.firstChild;
  }

  const WHAT = `
<p>${m2hleGame.title} keeps its settings and best times on the board, in its backup RAM.
<b>Keep it in this browser</b> and it is still there the next time you play here.</p>
<p>It stays on this device: nothing is sent anywhere. Say no and every visit starts on a
fresh board. You can change this at any time with the <b>Your save</b> button;
turning it off deletes the kept save.</p>`;

  function dialog(title, extra) {
    const box = el(`<div class="stfa-banner" role="dialog" aria-label="${title}">
      <h2>${title}</h2>${WHAT}${extra}
      <div class="stfa-buttons">
        <button data-a="keep">Keep my save</button>
        <button data-a="none">Don't keep it</button>
      </div></div>`);
    box.addEventListener('click', (e) => {
      const a = e.target.dataset && e.target.dataset.a;
      if (!a) return;
      apply(a === 'keep');
      box.remove();
    });
    document.body.append(box);
  }

  function panel() {
    const now = choice();
    dialog('Your save', `<p>Now: <b>${now ? 'kept in this browser' : 'not kept'}</b>.</p>`);
  }

  function start() {
    const css = document.createElement('link');
    css.rel = 'stylesheet';
    css.href = 'arcade/stf-arcade.css';
    document.head.append(css);
    const button = el('<button class="stfa-open" title="Your save">Your save</button>');
    button.addEventListener('click', panel);
    document.body.append(button);
    if (choice() === null) dialog('Keep your save?', '');
  }

  if (document.body) start(); else addEventListener('DOMContentLoaded', start);

  return {
    keep,
    beforeLoad() {
      if (window.Module && Module._web_backup_keep) Module._web_backup_keep(keep() ? 1 : 0);
      loaded = true;
    },
  };
})();
