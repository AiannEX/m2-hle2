# srally.sonicthefighte.rs

The Sega Rally page is play.sonicthefighte.rs under another name: the same files,
and `web/site/m2hle-game.js` picks Sega Rally when the host name starts `srally.`.
So the site holds no build of its own. One file differs: for `/` it serves
`srally.html`, which the build writes beside `index.html` with Sega Rally's
title and description, so a link preview (which runs no script) names Sega
Rally (Pinboard #673). A Caddy container on the RPCN droplet
(`srally-web`, on the `proxy` network) proxies play.sonicthefighte.rs, and srally
is always whatever the pages workflow last published (Pinboard #649). Until #649
it served a copy of `build_web/site` deployed by hand, which fell behind play.

On the droplet (143.198.49.181):

- `/root/srally-web/Caddyfile.web` is [Caddyfile](Caddyfile), mounted into the
  container. `/root/srally-web/Caddyfile.web.static-20261012` is the old
  static-copy config, kept for a rollback (with `/srv/srally-web`, the r675 copy).
- `/root/forgejo/Caddyfile` has the `srally.sonicthefighte.rs` block: TLS with the
  `*.sonicthefighte.rs` origin certificate, `reverse_proxy srally-web:8080`.

To change it, edit the file here, copy it over `/root/srally-web/Caddyfile.web`
and `docker restart srally-web`. Check with

```
curl -s https://srally.sonicthefighte.rs/ | grep -o 'r[0-9]*-[0-9a-f]\{7\}' | sort -u
```

which must print the same version as play.sonicthefighte.rs.
