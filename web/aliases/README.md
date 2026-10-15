# play.sonicthefighte.rs and srally.sonicthefighte.rs

Since Pinboard #881 every game is on **arcade.sonicthefighte.rs** (GitHub Pages):
`?game=srallyc` plays Sega Rally, and `srallyc.html` is the same page with Sega
Rally's title and description in its head, for a link preview (which runs no
script). play.sonicthefighte.rs and srally.sonicthefighte.rs are only aliases:

| Old address | Goes to |
|---|---|
| `play.sonicthefighte.rs/` | `arcade.sonicthefighte.rs/sfight.html` |
| `srally.sonicthefighte.rs/` | `arcade.sonicthefighte.rs/srallyc.html` |
| either, `/m2hle-migrate.html` | served there (below) |
| either, any other path | the same path on arcade |

The query string is kept (`?follow=`, `?ggpo=`, `?wheel=`...): an old link to a
followed board or a GGPO match still works.

## Why not a Cloudflare redirect rule

A browser keeps a site's storage per host. Everything a player kept on the old
hosts (the RPCN and GGPO sign-ins, the backup RAM, the arcade's cookies, keys,
pads, settings) is not on arcade, so a returning player looks signed out, on a
fresh board. [m2hle-migrate.html](m2hle-migrate.html) is how the arcade gets it
back: `web/site/m2hle-migrate.js` frames it from each old host once and copies
what the arcade does not have yet. A redirect rule cannot serve that page, so
the old hosts must reach this directory's [Caddyfile](Caddyfile), not a rule.
(The rule as first set up also dropped the query.)

## Deployed

The `srally-web` container on the RPCN droplet (143.198.49.181, on the `proxy`
network) runs the Caddyfile. `/root/forgejo/Caddyfile` routes both names to it
with the `*.sonicthefighte.rs` origin certificate:

```
play.sonicthefighte.rs, srally.sonicthefighte.rs {
	tls /etc/caddy/certs/sonicthefighte.rs.pem /etc/caddy/certs/sonicthefighte.rs.key
	reverse_proxy srally-web:8080
}
```

(use the `tls` line the `srally.sonicthefighte.rs` block already has). To update:
copy the Caddyfile over `/root/srally-web/Caddyfile.web`, `m2hle-migrate.html`
into the directory the container mounts at `/srv`, and `docker restart srally-web`.
In Cloudflare, play and srally are proxied DNS records for the droplet, with no
redirect rule for them. Check with

```
curl -sI 'https://play.sonicthefighte.rs/?follow=x' | grep -i '^location'    # .../sfight.html?follow=x
curl -sI https://srally.sonicthefighte.rs/ | grep -i '^location'              # .../srallyc.html?
curl -sI -H 'Origin: https://arcade.sonicthefighte.rs' \
  https://play.sonicthefighte.rs/m2hle-migrate.html | grep -i '^HTTP\|allow-origin\|frame-ancestors'
```

`/root/srally-web/Caddyfile.web.static-20261012` is the older static-copy config.
