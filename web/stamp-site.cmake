# Stamp the build's version into the copied page.
#
#   cmake -DSITE=<dir> -DVERSION=<version> -P web/stamp-site.cmake
#
# index.html names every other file with ?v=<version>, and the page script does
# the same for the two files it loads itself (m2hle.wasm, the audio worklet).
# GitHub Pages serves everything with a 10 minute cache lifetime and no way to
# change it, so without this a visitor arriving just after a deploy can get the
# new m2hle.js against the old page script, or the old .wasm against the new .js
# -- and those are built as a pair. With it, one index.html always asks for one
# consistent set.
#
# The stylesheet names its fonts (fonts/*.woff2) the same way, and index.html
# preloads them under those very URLs, so both files are stamped.
foreach(page index.html m2hle.css)
  file(READ "${SITE}/${page}" text)
  string(REPLACE "@M2HLE_VERSION@" "${VERSION}" text "${text}")
  file(WRITE "${SITE}/${page}" "${text}")
endforeach()

# srally.sonicthefighte.rs serves these same files, and m2hle-game.js gives the
# page Sega Rally's title once it runs. A link preview (Discord, a chat app, a
# search engine) reads the page without running it, so srally's proxy serves
# srally.html for / instead: index.html with Sega Rally's words in its head
# (web/srally/Caddyfile, Pinboard #673). The same text as m2hle-game.js sets.
file(READ "${SITE}/index.html" text)
set(sonic_title "<title>Sonic the Fighters</title>")
set(sonic_desc "content=\"Play Sonic the Fighters in your browser, online, with your own copy of the game.\"")
foreach(want sonic_title sonic_desc)
  string(FIND "${text}" "${${want}}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "stamp-site: index.html no longer has ${${want}}; update srally.html's words here")
  endif()
endforeach()
string(REPLACE "${sonic_title}" "<title>Sega Rally Championship</title>" text "${text}")
string(REPLACE "${sonic_desc}"
  "content=\"Play Sega Rally Championship in your browser, with your own copy of the game.\"" text "${text}")
file(WRITE "${SITE}/srally.html" "${text}")
