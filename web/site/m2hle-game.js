/*
 * Which game this page plays. Loaded after m2hle-games.js, which picked it
 * from the address (?game=ID&profile=PROFILE, Pinboard #881); the key and pad
 * panels read their row labels from this, and the page script its arguments
 * and ROM text. The profile reaches main_web.c as --profile=ID; ?wheel=range,speed
 * (or ?wheel=range) adjusts Sega Rally's steering pot (src/board/io_5649.h; the
 * default is the operator manual's spec).
 */
'use strict';

const m2hleGame = (() => {
  const params = new URLSearchParams(location.search);
  /* index.html sends a visit with no game to the front page before this runs;
   * Sonic the Fighters only if that did not happen yet. */
  const picked = m2hleGames.picked || { id: 'sfight', profile: 'sfight_console' };
  const id = picked.id;
  const game = Object.assign({ id, profile: picked.profile }, m2hleGames.GAMES[id],
                             { args: ['--profile=' + picked.profile] });

  if (id === 'srallyc') {
    const w = (params.get('wheel') || '').split(',').map((v) => v.trim());
    if (/^(0x[0-9a-f]+|\d+)$/i.test(w[0] || '')) game.args.push('--wheel-range=' + w[0]);
    if (/^(0x[0-9a-f]+|\d+)$/i.test(w[1] || '')) game.args.push('--wheel-speed=' + w[1]);
  }

  /* The page's own words for the game (index.html's are the whole arcade's). */
  document.title = game.title;
  const head = document.querySelector('#panel .ps-title');
  if (head) head.textContent = game.title;
  const hint = document.querySelector('#drop .hint');
  if (hint) hint.innerHTML = game.romHint;
  const desc = document.querySelector('meta[name="description"]');
  if (desc) desc.content = 'Play ' + game.title + ' in your browser, with your own copy of the game.';

  /* The arcade (arcade/stf-arcade.mjs): a shared board per game, and whether this
   * browser keeps the game's own. ?game= tells it which board and which words: Sega
   * Rally's own, not the Fly's, which are Sonic the Fighters'. Named with this
   * script's own ?v=, as index.html names everything. */
  if (document.currentScript) {
    const s = document.createElement('script');
    const q = new URLSearchParams(new URL(document.currentScript.src).search);
    q.set('game', id);
    s.type = 'module';
    s.src = 'arcade/stf-arcade.mjs?' + q;
    document.head.append(s);
  }

  return game;
})();
