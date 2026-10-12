/*
 * Which game this page plays. Loaded first: the key and pad panels read their
 * row labels from it, and the page script its arguments and ROM text.
 *
 * ?game=srallyc picks Sega Rally, and so does a host named srally.* (the
 * srally.sonicthefighte.rs site serves these same files). Anything else is
 * Sonic the Fighters, as before. The game reaches main_web.c as --profile=ID;
 * ?wheel=range,speed (or ?wheel=range) adjusts Sega Rally's steering pot
 * (src/board/io_5649.h; the default is the operator manual's spec).
 */
'use strict';

const m2hleGame = (() => {
  const params = new URLSearchParams(location.search);
  const GAMES = {
    sfight: {
      title: 'Sonic the Fighters',
      args: [],
      romHint: 'A merged <code>sfight.zip</code> or <code>schamp.zip</code>',
      labels: {},
    },
    srallyc: {
      title: 'Sega Rally Championship',
      args: ['--profile=srallyc'],
      romHint: 'Your <code>srallyc.zip</code> (Sega Rally Championship, Twin/DX)',
      /* src/profiles/srallyc.h: LEFT/RIGHT the wheel, B1 accelerator, B2 brake,
       * UP/DOWN a gear, B3 VIEW CHANGE. */
      labels: { up: 'Shift up', down: 'Shift down', left: 'Steer left', right: 'Steer right',
                b1: 'Accelerator', b2: 'Brake', b3: 'View change', b4: 'Unused' },
    },
  };
  const asked = params.get('game');
  const id = GAMES[asked] ? asked : /^srally\./.test(location.hostname) ? 'srallyc' : 'sfight';
  const game = Object.assign({ id }, GAMES[id]);

  if (id === 'srallyc') {
    const w = (params.get('wheel') || '').split(',').map((v) => v.trim());
    if (/^(0x[0-9a-f]+|\d+)$/i.test(w[0] || '')) game.args.push('--wheel-range=' + w[0]);
    if (/^(0x[0-9a-f]+|\d+)$/i.test(w[1] || '')) game.args.push('--wheel-speed=' + w[1]);
  }

  /* The page's own words for the game (index.html is Sonic the Fighters'). */
  document.title = game.title;
  const head = document.querySelector('#panel .ps-title');
  if (head) head.textContent = game.title;
  const hint = document.querySelector('#drop .hint');
  if (hint) hint.innerHTML = game.romHint;
  const desc = document.querySelector('meta[name="description"]');
  if (desc) desc.content = 'Play ' + game.title + ' in your browser, with your own copy of the game.';

  /* The arcade (arcade/stf-arcade.mjs) is the Fly's board and ranking, which are
   * Sonic the Fighters': another game keeps its own save (m2hle-save.js). Named
   * with this script's own ?v=, as index.html names everything. */
  if (id === 'sfight' && document.currentScript) {
    const s = document.createElement('script');
    s.type = 'module';
    s.src = 'arcade/stf-arcade.mjs' + new URL(document.currentScript.src).search;
    document.head.append(s);
  }

  return game;
})();
