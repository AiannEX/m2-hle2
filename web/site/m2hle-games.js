/*
 * The games this site plays, and which one a visit asked for (Pinboard #881).
 * Loaded first by both pages: home.html draws a box per game from it, and
 * index.html runs the one picked (m2hle-game.js).
 *
 *   arcade.sonicthefighte.rs/?game=sfight&profile=sfight_console
 *
 * ?game= is a key of GAMES, ?profile= one of its profiles (the first is the
 * default, as in src/profiles/registry.h). Without ?game=, a page named for a
 * game (sfight.html, srallyc.html: web/stamp-site.cmake) or a host named for
 * one (play.*, srally.*: the old sites, now aliases) picks it, and any other
 * query (?follow=, ?rom=, ...) is Sonic the Fighters, as it was before there
 * was a front page. A bare visit, or an unknown ?game=, is the front page.
 *
 * Each game's profiles must be in the web build (registry.h, M2HLE_WEB).
 */
'use strict';

const m2hleGames = (() => {
  const GAMES = {
    sfight: {
      title: 'Sonic the Fighters',
      year: 1996, board: 'Model 2B-CRX', colors: ['#1d4fd8', '#0b1d5c'],
      blurb: 'Sonic and friends fight it out on the Death Egg. Online play over RPCN and GGPO.',
      profiles: [
        { id: 'sfight_console', name: 'Console', about: 'The PS3 release: its menus, free play' },
        { id: 'sfight', name: 'Arcade', about: 'The cabinet as it was' },
      ],
      romHint: 'A merged <code>sfight.zip</code> or <code>schamp.zip</code>',
      labels: {},
    },
    srallyc: {
      title: 'Sega Rally Championship',
      year: 1995, board: 'Model 2A-CRX', colors: ['#d8361d', '#5c1a0b'],
      blurb: 'Desert, forest, mountain and lakeside: the rally that started it all.',
      profiles: [
        { id: 'srallyc', name: 'Arcade', about: 'Twin/DX, Rev C' },
      ],
      romHint: 'Your <code>srallyc.zip</code> (Sega Rally Championship, Twin/DX)',
      /* src/profiles/srallyc.h: LEFT/RIGHT the wheel, B1 accelerator, B2 brake,
       * UP/DOWN a gear, B3 VIEW CHANGE. */
      labels: { up: 'Shift up', down: 'Shift down', left: 'Steer left', right: 'Steer right',
                b1: 'Accelerator', b2: 'Brake', b3: 'View change', b4: 'Unused' },
    },
  };
  const HOSTS = { play: 'sfight', srally: 'srallyc' };

  /* { id, profile } for this visit, or null for the front page. */
  function pick(loc) {
    const q = new URLSearchParams(loc.search);
    const page = (/\/([a-z0-9_]+)\.html$/.exec(loc.pathname) || [])[1];
    const host = (/^([a-z]+)\./.exec(loc.hostname) || [])[1];
    let id = null;
    if (q.has('game')) id = GAMES[q.get('game')] ? q.get('game') : null;
    else if (GAMES[page]) id = page;
    else if (HOSTS[host]) id = HOSTS[host];
    else if (loc.search.length > 1) id = 'sfight';
    if (!id) return null;
    const profiles = GAMES[id].profiles;
    const profile = profiles.find((p) => p.id === q.get('profile')) || profiles[0];
    return { id, profile: profile.id };
  }

  /* The link that plays a game, relative to the site's root. */
  function href(id, profile) {
    const q = new URLSearchParams({ game: id, profile: profile || GAMES[id].profiles[0].id });
    return './?' + q;
  }

  return { GAMES, pick, href, picked: pick(location) };
})();
