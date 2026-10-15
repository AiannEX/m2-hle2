/*
 * The front page's boxes (home.html), one per game in m2hle-games.js. The box
 * plays the game's first profile; a game with more than one lists them all
 * under it, each its own link.
 */
'use strict';

(() => {
  /* The address is the site's root, not home.html: index.html sent the visit
   * here only because it named no game, and the root is the link to share. */
  if (/\/home\.html$/.test(location.pathname)) history.replaceState(null, '', './');

  const make = (tag, props, ...kids) => {
    const el = Object.assign(document.createElement(tag), props);
    el.append(...kids);
    return el;
  };

  function box(id, g) {
    const main = make('a', { href: m2hleGames.href(id) },
      make('div', { className: 'marquee' }, g.title),
      make('div', { className: 'about' },
        make('div', { className: 'meta' }, g.year + ' · ' + g.board),
        make('p', {}, g.blurb)));
    main.querySelector('.marquee').style.cssText = `--c0:${g.colors[0]};--c1:${g.colors[1]}`;
    const profiles = make('div', { className: 'profiles' },
      ...g.profiles.map((p) => make('a', { href: m2hleGames.href(id, p.id), title: p.about }, p.name)));
    return make('li', { className: 'game' }, main, profiles);
  }

  const list = document.getElementById('games');
  for (const [id, g] of Object.entries(m2hleGames.GAMES)) list.append(box(id, g));
})();
