# Game scripts (M2emulator's Lua, Pinboard #958)

Code: `src/core/m2lua.h` (the runtime), `src/ui/m2lua_ui.h` (the desktop's Script
menu and drawing), `src/ui/m2lua_mcp.h` (the bridge's `script_*`),
`web/site/m2hle-cheats.js` with `web_cheats_*` in `src/main_web.c` (the web's
Cheats tab). Lua 5.5.1 is the `vendor/lua` submodule, built from `onelua.c`.
Test: `m2lua_test`.

## What M2emulator did (read off EMULATOR.EXE with rea + Ghidra)

- One script per ROM set, `SCRIPTS\<set>.lua`, loaded once at game load. The
  chunk runs, then the global `Options` table is walked ONCE: `name` (default the
  key), `values` (default `{"Off","On"}`), whether `runfunc` / `changefunc` are
  functions. Options added later are never seen.
- Callbacks: `Init` after the load, `Reset` after a board reset, `Frame` after each
  frame, then `runfunc(value)` for every option whose value is not 0, then
  `PostDraw`; `End` at unload. A menu change sets `Options.<key>.value` and calls
  `changefunc(value)`.
- `AND OR XOR NOT SHL SHR` round each argument, work on the low 32 bits and return
  an UNSIGNED 32-bit integer; `SHR` is logical, the count is mod 32. Lua 5.5's own
  `& | ~ << >>` work beside them, but on 64-bit integers: mask with `& 0xFFFFFFFF`.
- `Romset_*` areas are M2emulator's region numbers 0-11, mapped to ours through
  `{0,1,2,3,4,5,6,-1,-1,-1,7,-1}` (-1: nothing; reads give 0).

## Rules

- **The Lua state is touched only with the emu mutex held** (frame edge, reset,
  load, option change, eval), M2emulator's critical section. PostDraw's list goes
  to the window through `draw_mutex` (`m2lua_take_draw`).
- **A script never runs while a session owns the board**: `emu_script_may_run()`
  (`emu_thread.h`) is false during RPCN, GGPO, replay playback, and while
  following or leading a follow. Then the script is suspended and every ROM byte
  it patched is put back (the undo log, `m2lua_unpatch`). When the session ends
  it runs again FROM ITS START (Init re-patches), with the options it had. So
  a script changes nothing the i960 can read during a session, and needs no
  `NETPLAY_PROTO_REV` bump. Proved 2026-10-16: one client with a script writing
  work RAM every frame, one without, 1100 frames on the local RPCN, no desync.
- Bridge `script_option` / `script_eval` / `script_reload` are refused while a
  session owns the board.
- `Romset_Patch*` writes through `m2lua_patch`, which logs the old byte. Never
  write `g_m2lua.rom` regions any other way.
- The desktop reads `<dir>/<set>.lua` (`--scripts DIR`, default `scripts/` in the
  cwd or beside the executable; `--no-scripts`). The web has no files: the Cheats
  tab's text goes in with `m2lua_load_source` and is kept in localStorage
  (`m2hle.cheats.<set>`, `.on`, `.opts`). Given during a session, the text waits,
  unrun, until the session ends.
- The web draws text only (`web_cheats_draw`, on a canvas over the picture);
  `Video_CreateSurfaceFromFile` has no file to read there.
- `Model2_Set*` (widescreen, stretch) are kept and reported, not drawn.
