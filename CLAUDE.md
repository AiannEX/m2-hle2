# CLAUDE.md

Seeds a fresh Claude session with the hard-won facts that took trial-and-error to discover — things that cannot be re-derived from the i960 manual or general emulator-design knowledge.

[PROPOSAL.md](PROPOSAL.md) is the original plan: its §3 has the board-vs-game rationale, and its §8 is an older subset of this file. This file is for the *invariants that must never be re-derived*; the proposal is historical context.

---

## Project Overview

A general **Sega Model 2 arcade emulator**, written in C11 with Dear ImGui (via cimgui) and Sokol for cross-platform graphics. The first target is *Sonic The Fighters* (STF), because the bulk of the prior reverse-engineering work happened there — but the architecture is built for the **full Model 2 catalogue** from day one. Generalising across games strengthens every subsystem: most "STF bugs" turn out to be board-level i960 / COP / tile bugs that affect every Model 2 game equally.

**Reference prior project** — the previous full implementation lives at `c:\Users\bigge\source\repos\ai\m2-hle\` (same `src/{board,core,ui,profiles}` layering, corrected COP math, 68K + SCSP, MCP harness). Treat it as a working reference for register-window logic, COP math, the polygon decoder, HLE hook patterns, and the memory-region table. The older STF-only `c:\Users\bigge\source\repos\stf-hle\` still exists but is badly outdated — consult it only when asked. Read freely from m2-hle; do not import code wholesale — the new project's layering (board vs game profile, §3 below) means files will need restructuring as they're brought over.

See [PROPOSAL.md](PROPOSAL.md) for the architecture, module map, build commands, and bootstrap checklist.

---

## Board vs. Game Layering (critical)

Code lives in one of two layers. Get this distinction wrong and you'll re-implement board-level fixes in per-game files.

- **Board layer** — anything shared by every Model 2 ROM set: i960 CPU core, memory bus, COP math, tile renderer, 3D polygon decoder, sound block, threading. Lives in shared `.h` modules.
- **Game-profile layer** — anything specific to one ROM set: HLE hook table (addresses), input map, ROM file list + CRC32s, optional quirks struct. Lives in a `game_profile_t` entry resolved from the ROM set's name (the zip basename, `profile_for_rom_set`); the CRC32s validate each file.

**Default to the board layer.** If a bug surfaces in STF, your first hypothesis should be "this is board-level and another game is also affected" — not "this is STF-specific." Only move a fix to the per-game layer when you have positive evidence (e.g. another game's ROM relies on the opposite behaviour).

---

## Implementation Invariants (do not re-derive)

These are facts reverse-engineered or debugged into the original implementation. Not in any datasheet. Treat as load-bearing.

They live in [docs/invariants/](docs/invariants/), one file per subsystem, so a session loads only what it needs. **Before you change a subsystem, read its file**; a comment that cites CLAUDE.md for one of these means the file of that title. Add what you find to the matching file (or a new one, with a line here), never to this file: CLAUDE.md is read in full on every turn of every session.

- [i960 CPU (board-level — every Model 2 game)](docs/invariants/i960.md)
- [Coprocessor (COP) — board-level math, every Model 2 game](docs/invariants/cop.md)
- [3D Polygon Decoder (board-level — confirmed against two games)](docs/invariants/polygon-decoder.md)
- [Memory Bus (board-level)](docs/invariants/memory-bus.md)
- [Tile Renderer (board-level)](docs/invariants/tile-renderer.md)
- [Sound board (board-level — `sound.h`, `scsp.h`, `m68k_exec.h`)](docs/invariants/sound-board.md)
- [Backup RAM (`core/backup_ram.h`, board-level)](docs/invariants/backup-ram.md)
- [Savestates (`core/savestate.h`, board-level)](docs/invariants/savestates.md)
- [Following a board one way (`core/follow.h`, issue #228)](docs/invariants/follow.md)
- [Replays of online matches (`core/replay.h`, Pinboard #572)](docs/invariants/replays.md)
- [Homebrew on a game's board (`profiles/sfight_homebrew.h`)](docs/invariants/homebrew.md)
- [HLE Hooks (game-specific addresses, board-level patterns)](docs/invariants/hle-hooks.md)
- [Threading](docs/invariants/threading.md)
- [Raw A/V out (`--av-port`, host-side — `core/av_stream.h`, `ui/av_capture.h`)](docs/invariants/av-out.md)
- [Overlay plugin swap (`overlay_swap`, `ui/overlay_host.h`, host-side)](docs/invariants/overlay-swap.md)
- [Picture filters (`ui/post_shader.h`, `ui/retro_shader.h`, host-side)](docs/invariants/picture-filters.md)
- [Netplay / RPCN (`src/net/`, board-independent)](docs/invariants/netplay.md)
- [Rooms of more than two (`net/room.h`, after the PS3 port's Room Match)](docs/invariants/rooms.md)
- [The PS3 release's menus (`src/ui/ps3ui*.h`, libretro core + web build)](docs/invariants/ps3-menus.md)
- [Cross-play with the PS3 port (`net/ps3_link.h`, `rudp.h`, `rpcs3_signal.h`)](docs/invariants/ps3-crossplay.md)
- [Rollback netplay: GGPO (`core/emu_ggpo.h`, `core/rollback.h`, `net/ggpo_lobby.h`, Pinboard #575)](docs/invariants/ggpo.md)

---

## STF Disassembly Reference

Authoritative IDA disassembly: `C:\m2\ida72\asm-check\`.

| File | Description |
|------|-------------|
| `stf_prog.asm` | Full ~477K-line IDA disassembly — primary reference for STF ROM addresses, struct layouts, function names |
| `decomp/` | ~1057 per-function `.S` files named after game subsystems |
| `process-win.bat` | ROM rebuild chain: `m2asm.py` → `gcc960` → `objcopy` (intel960) → `stfbin2rom.py` → `epr-19001.15` / `epr-19002.16` |

**ROM identity:** CRC32 `72E66A1D`, MD5 `2A3E32834FC727391C0AFCB18121245E`.
**`stfbin2rom.py --ctools`** strips the 44-byte (`0x2C`) b.out header `gcc960` prepends.

For other Model 2 games, see **MAME** (`src/mame/sega/model2.cpp`) as a cross-reference for memory map, COP opcodes, and ROM region layouts. Don't copy code; do cross-check addresses.

---

## Build

`cmake` is on `PATH` (`C:\Program Files\CMake\bin\cmake`). The toolchain is **Visual Studio 2026 (VS 18)**, and `build\` is its tree: the graders launch it, and `build.ps1` and `run_tests.ps1` hard-code it. `build_vs22` is a VS 2022 tree from before the switch; do not build or stream from it. A tree configured for a generator that is not installed fails with "could not find specified instance of Visual Studio" — configure a fresh directory rather than reusing it.

Everything under `vendor/` is a **git submodule pinned to an exact upstream commit** — `imgui`, `dear_bindings`, `sokol`, `miniz`, `ImGuiFileDialog`, `imgui_club` (its `imgui_memory_editor` is the hex grid both memory viewers are built on), `stb` (stb_truetype, for the PS3-menu fonts; every frontend needs it), and `noclip` (the last only feeds `tools/`). A tree cloned without them fails configure with the `git submodule update --init …` line to run.

The cimgui C bindings are **not committed**: CMake runs `vendor/dear_bindings/dear_bindings.py` over `vendor/imgui/imgui.h` into `<build>/cimgui-gen/` at build time, with `--replace-prefix ImGui_=ig` to keep the `ig*` spelling that `src/` and `sokol_imgui.h`'s "original cimgui" path expect. That needs Python 3 with `ply` (`python -m pip install ply==3.11`); configure fails with the exact install line if the interpreter CMake picks up cannot import it. Do **not** swap this for the `cimgui/cimgui` repo — that is a different generator, and it produced an `ImGuiIO` ABI mismatch here (`MousePos` updated, `MouseDown` stuck at 0).

```
cmake -S <repo> -B <repo>/build -G "Visual Studio 18 2026" -A x64
cmake --build <repo>/build --config Release --target ALL_BUILD -j 16
```

The other frontends are `-DM2HLE_FRONTEND=sdl3` (the handheld, below), `web` (`emcmake cmake -S . -B build_web -DM2HLE_FRONTEND=web`; see WEB-PORT.md) and `libretro` (a RetroArch core; `-DM2HLE_LIBRETRO_GLES=ON` for GLES 3; see packaging/libretro/).

Output: `build\Release\m2hle.exe`. The unit tests in `tests/` build alongside it (`M2HLE_BUILD_TESTS`, on by default) and run under `ctest` or `run_tests.ps1`; CI (`.github/workflows/canary.yml`) runs every ROM-free one — `mem_test`, `i960_test`, `cop_test`, `m68k_test`, `emu_test`, `net_test`, `ps3net_test`, `tile_test`, `heat_test`, `scsp_dsp_test`, `scsp_dsp_test_masks`, `scsp_lazy_test`, `tgp_test`, `retro_shader_test`, `ra_info_cache_test`, `sfight_settings_test`, `prepared_test` — since `rom_test`, `boot_test`, `geo_test` and `input_test` load a ROM (`$ROMDIR`, else `$ROMS_DIR`, else the owner's Windows path; `tests/test_rom_dir.h`). `cop_replay`, `snd_replay`, `snd_bench`, `det_digest` and `ps3ui_render` are built but are not ctests: capture replays, benches and a renderer for the graders. Everything beyond that is graded by `tools/` or checked interactively. `--headless --mcp --rom <zip> --run` runs the emulator and its bridge with no window, GPU or audio device; the graders launch it that way (`$M2_WINDOW=1` shows the window). Nothing loads without `--rom <zip>` (or File → Load ROMs in the window); `schamp.zip` is found beside `sfight.zip`. **In the dev container every stock set is in `$ROMS_DIR` (`~/build/mameroms`): use it in place, never copy a zip into a scratch or build directory, and never run the emulator from the ROM's folder** (its log lands there, and a loose `sfight/epr-1900x` in the working directory replaces the zip's program; tools/README.md). The profile is picked by the set's name, and `--profile` or the Game menu chooses among profiles for the same set.

**The handheld build** (`-DM2HLE_FRONTEND=sdl3`) is the same board with a fullscreen SDL3/GLES 3 host and no ImGui — it runs on the Anbernic RG ARC-S (RK3566, ROCKNIX) at 58-60 game fps. CI no longer builds it: the libretro core (`m2hle-libretro-linux-arm64.zip`) replaced it on the handhelds (Pinboard #566), so build it by hand when you need it. [packaging/rocknix/](packaging/rocknix/) holds the launcher EmulationStation calls, the installer, the toolchain file and the notes. Two things there are load-bearing: the container is **debian:trixie**, the one distribution with `libsdl3-dev` for arm64 and ROCKNIX's own glibc 2.41, so the binary carries no libraries of its own; and `CMAKE_TOOLCHAIN_FILE` must be **absolute**, since a relative one is looked for from the build directory and CMake then quietly configures for the host. Netplay's TLS on Linux is the system OpenSSL opened with `dlopen` (`net/tls.h`), not linked, so it does not add a fifth library; do not "fix" that with `-lssl`.

**Grading harness** — [tools/](tools/) measures this emulator against an independent implementation of the same ROM formats (the STF explorer, a submodule at `vendor/noclip`), with SHA-256 over a MAME capture as a third point so the two ports cannot simply agree with each other and be wrong together. `node tools/grade-models.mjs` is the one to run after touching `geo3d.h`, and `node tools/grade-pose.mjs` after touching the COP bone handlers in `sharc_exec.h` — the latter replays 328 frames of rig arguments captured off a real board, so it needs a sibling `stf-tools` checkout for `motion-pose.csv` and skips cleanly without one. `node tools/grade-cull.mjs` checks which arena ground chunks get drawn against the ROM's own `area_clip` rule, on the camera the display list was drawn from. `node tools/grade-stages.mjs` plays a round on each of the fifteen stages (picked at ROUND_INIT, `0xAFC8`). It checks every arena part on the stage object clocks, the moving stages' flights, the matrices the COP lays into the display list and the texture scrolls, all against the explorer. Run it after touching the COP matrix handlers or anything a stage routine calls. `node tools/grade-zsort.mjs --stage 1` / `--stage 5` holds the 3D pictures against MAME's where faces lie on faces, on the same replay; run it after touching the z-sort in `geo3d.h` or the fill shader's depth. `node tools/match-replay.mjs` plays attract's preprogrammed Sonic vs Bean replay fight (`--match-replay` skips the intro movie) and holds both fighters frame by frame against a MAME reference taken with `--mame`. The fight is an input replay, so any divergence is a simulation bug; run it after touching the i960 core or any COP handler the fight uses. `node tools/grade-reset.mjs` needs no oracle at all: it boots, plays into attract, performs the netplay barrier's reset with no session (`board_reset` over the bridge) and holds the boot that follows against the first one, byte for byte, twice. Run it after adding any state a board reset has to clear -- a static in a hook, a latch in the run loop, a region in `mem_init`. `node tools/ab-builds.mjs` holds two *builds* to the same board state (an optimisation's proof), `node tools/bench-builds.mjs` times them headless, and `node tools/bench-render.mjs` times the renderer's stages on the real D3D11 device; `tile_test` (a ctest) holds the tile compositor to its pixel-by-pixel original. See [tools/README.md](tools/README.md).

---

## Conventions

- All modules except the entry points (`main.c`, `main_sdl.c`, `main_web.c`, `main_libretro.c`), the sokol implementation units (`sokol_*impl.c`, `sokol_impl.m`) and the vendored `miniz.c` are **header-only `.h` files**. This is intentional — do not split into `.c`/`.h` pairs.
  - The one deliberate exception is `src/ui/mem_edit.cpp`, the single C++ translation unit: `vendor/imgui_club`'s `MemoryEditor` is a C++ struct against the ImGui C++ API, and C11 sources cannot include it. It hands out the C handle declared in `mem_edit.h`; keep C++ from spreading past that file.
  - `src/net/ggpo_port/ggpo_port.cpp` is the other: GGPO is a C++ library, and that file is its platform layer and the C API (`ggpo_port.h`) the rest of the tree calls.
- **A game no board here can run yet is a prepared profile** (`g_prepared_profiles`, `registry.h`; Pinboard #660): a ROM recipe (`core/rom_recipe.h`, MAME's ROM_START lines as data, the shape of noclip's `regions`), the model table noclip reads, no install function, and a `preliminary` sentence saying what the board lacks, which every front end shows instead of "no profile". Virtua Cop, Daytona USA and The House of the Dead are prepared; making one run means moving it into `g_profiles`. The model-table entry layout is the profile's (`game_quirks_t.model_layout`, `board/model_table.h`: 1995's, Virtua Cop's, Daytona's, as noclip's `readModelEntry`); read the table only through `profile_model_table` + `model_entry_read`, never at fixed +0/+4/+8, and turn a mesh pointer into a polygon-ROM offset only with `model_mesh_resolve` (noclip's `meshOffsetOf` plus `decodeModel`'s bound; polygon RAM is refused). Whether a model has a texture header stream is `geo3d_model_have_mat` (an address of 0 counts where the layout says `mat_at_zero`: Virtua Cop's). `geo3d_mesh_reads` reports what the decoder reads of a mesh (a polygon-ROM model's, or with `obj_mesh` a polygon-RAM object's), and the header fields it decodes, and `prepared_test` holds both, and the emitted normals and bit 128, to noclip's `decodeModel`. NOCLIP-SYNC.md §6.3 to §6.8 have the parity checks; §6.8 has the ones made against MAME's RAM dumps.
- Default new code to the **board layer**; only move to a `game_profile_t` quirk when there's positive evidence of game-specific behaviour.
- **Desktop-only debugger hooks in the board go behind `M2HLE_DEV_TOOLS`** (`core/build_features.h`): 1 for the desktop and the tests, 0 for sdl3, libretro and web (CMakeLists.txt). Test an "is it armed" helper (`bp_armed`, `wp_armed`, `dl_active`, `sndcap_on`), never the raw field, so the check is a constant false where nothing can arm it. Keep the state and functions defined either way: det_digest is built in the web tree. Platform code is gated where it is used by the compiler's macros (`_WIN32`, `__linux__`, `__EMSCRIPTEN__`).
- Memory addresses and sizes use `uint32_t`. Sign-extension is handled per-instruction.
- Platform threading is abstracted in `thread_mutex.h` (pulled in by `emu_thread.h`): `emu_mutex_lock()` / `emu_mutex_unlock()` wrap `CRITICAL_SECTION` on Windows and `pthread_mutex_t` on POSIX.
- Logging: `log_msg(severity, fmt, ...)` from `log.h` (levels `LOG_LVL_*`), or the `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` macros that wrap it. **Log unknown COP commands and unhandled MMIO at WARN** so new-game support work surfaces them automatically.
