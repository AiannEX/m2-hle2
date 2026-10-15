# srally.sonicthefighte.rs

Since Pinboard #881 every game is on **arcade.sonicthefighte.rs** (GitHub Pages):
`?game=srallyc` plays Sega Rally, and `srallyc.html` is the same page with Sega
Rally's title and description in its head, for a link preview (which runs no
script). srally.sonicthefighte.rs and play.sonicthefighte.rs are only aliases:

| Old address | Goes to |
|---|---|
| `srally.sonicthefighte.rs/` | `arcade.sonicthefighte.rs/srallyc.html` |
| `play.sonicthefighte.rs/` | `arcade.sonicthefighte.rs/sfight.html` |
| either, any other path | the same path on arcade |

Keep the query string (`?follow=`, `?ggpo=`, `?wheel=`...). The page also picks the
game from a host named `srally.*` or `play.*`, so a proxy instead of a redirect
works too.

The redirect can be a Cloudflare redirect rule, or this directory's
[Caddyfile](Caddyfile) in the `srally-web` container on the RPCN droplet
(143.198.49.181, on the `proxy` network; `/root/forgejo/Caddyfile` has the
`srally.sonicthefighte.rs` block with the `*.sonicthefighte.rs` origin
certificate, `reverse_proxy srally-web:8080`). To change it, copy the file over
`/root/srally-web/Caddyfile.web` and `docker restart srally-web`. Check with

```
curl -sI https://srally.sonicthefighte.rs/ | grep -i '^location'
```

Until #881 the container proxied play.sonicthefighte.rs and served `srally.html`
for `/` (Pinboard #649, #673); `/root/srally-web/Caddyfile.web.static-20261012`
is the older static-copy config.
