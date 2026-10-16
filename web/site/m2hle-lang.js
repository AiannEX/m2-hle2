/*
 * The site's languages (Pinboard #935). Loaded first by both pages, before
 * m2hle-games.js.
 *
 *   arcade.sonicthefighte.rs/?lang=ja&game=vonj
 *
 * ?lang= is a key of LANGS; without it, or with one the site does not have, the
 * page is in English. The language is the page's words only: which game and
 * which profile run is ?game= and ?profile= (m2hle-games.js), so a Japanese
 * page can play an English set and the other way round.
 *
 * A language is a table of strings by key. English is the template: every key
 * is in it, and a key another language leaves out shows its English, so a new
 * language can start with the few lines that matter and grow. To add one, copy
 * `en`, translate, and add it to LANGS.
 *
 * The page names what to translate with attributes:
 *   data-i18n="key"       the element's text
 *   data-i18n-html="key"  its HTML (only for the tables' own markup, <code>)
 *   data-i18n-title="key", data-i18n-aria="key"  its title / aria-label
 * A game's own words (title, blurb, profile names, ROM hint, control labels)
 * are in its GAMES entry, with each language's under `i18n` (m2hleGames.text).
 */
'use strict';

const m2hleLang = (() => {
  const en = {
    'home.title': 'Model 2 Arcade',
    'home.lead': 'Sega Model 2 games in your browser, run by m2-hle2. Bring your own copy of the game: it stays on your computer.',
    'home.noscript': 'This page needs JavaScript to run the games.',
    'home.soon': '{about}: not in the emulator yet',
    'home.desc': 'Play Sega Model 2 arcade games in your browser, with your own copy of the game.',
    'site.source': 'Source on GitHub',
    'site.games': 'All games',
    'site.language': 'Language',
    'game.desc': 'Play {title} in your browser, with your own copy of the game.',
    'step.loading': 'Starting up…',
    'step.rom': 'Add your copy of the game to begin.',
    'step.choose': 'Choose your game file',
    'step.drop': 'or drop it here',
    'step.private': 'Your file stays on this computer. It is read by this page and is never uploaded.',
    'step.busy': 'Reading your game file…',
    'step.fatal': 'This browser could not start the game.',
    'step.reload': 'Reload',
    'hint.rom': ':Choose file',
  };

  const ja = {
    'home.title': 'Model 2 アーケード',
    'home.lead': 'セガ Model 2 のゲームをブラウザで。エミュレーターは m2-hle2 です。ゲームはご自分のものをお使いください。ファイルはお使いのコンピューターから出ません。',
    'home.noscript': 'ゲームを動かすには JavaScript が必要です。',
    'home.soon': '{about}：エミュレーター未対応',
    'home.desc': 'セガ Model 2 のアーケードゲームを、ご自分のゲームファイルでブラウザから遊べます。',
    'site.source': 'GitHub のソース',
    'site.games': 'ゲーム一覧',
    'site.language': '言語',
    'game.desc': '{title} をご自分のゲームファイルでブラウザから遊べます。',
    'step.loading': '起動中…',
    'step.rom': 'はじめにゲームファイルを選んでください。',
    'step.choose': 'ゲームファイルを選ぶ',
    'step.drop': 'またはここにドロップ',
    'step.private': 'ファイルはこのページが読み込むだけで、アップロードはされません。',
    'step.busy': 'ゲームファイルを読み込み中…',
    'step.fatal': 'このブラウザではゲームを起動できませんでした。',
    'step.reload': '再読み込み',
    'hint.rom': '：ファイルを選ぶ',
  };

  /* name: the language's name in itself, for the switch. */
  const LANGS = { en: { name: 'English', strings: en }, ja: { name: '日本語', strings: ja } };
  const DEFAULT = 'en';

  const q = new URLSearchParams(location.search);
  const lang = LANGS[q.get('lang')] ? q.get('lang') : DEFAULT;

  /* The string for `key` in this page's language, {name}s filled from `vars`. */
  function t(key, vars) {
    const s = LANGS[lang].strings[key] ?? en[key] ?? key;
    return vars ? s.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? vars[k] : m)) : s;
  }

  /* Translate everything under `root` that names a key. */
  function apply(root) {
    const all = (attr, fn) => root.querySelectorAll('[' + attr + ']')
      .forEach((el) => fn(el, el.getAttribute(attr)));
    all('data-i18n', (el, k) => { el.textContent = t(k); });
    all('data-i18n-html', (el, k) => { el.innerHTML = t(k); });
    all('data-i18n-title', (el, k) => { el.title = t(k); });
    all('data-i18n-aria', (el, k) => el.setAttribute('aria-label', t(k)));
  }

  /* `params` (a URLSearchParams) with this page's language, if it is not the
   * default, so a link keeps the visit's language. */
  function keep(params) {
    if (lang !== DEFAULT) params.set('lang', lang);
    return params;
  }

  /* The query a link to a page with no other parameters needs: '' or '?lang=..'. */
  function query() {
    const q = keep(new URLSearchParams()).toString();
    return q ? '?' + q : '';
  }

  document.documentElement.lang = lang;
  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', () => apply(document));
  else apply(document);

  return { LANGS, DEFAULT, lang, t, apply, keep, query };
})();
