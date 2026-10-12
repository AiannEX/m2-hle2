# Bubblegum fixes: an audit (Pinboard #245, refreshed in #674)

This file lists the places where m2-hle2 patches a symptom and the board has a simpler rule underneath. A patch here means a list of model numbers or addresses, a fitted threshold, a hook that pokes RAM, or a per-game switch for something every board does.

The first audit was of master at `28d3563` (2026-09-30). This refresh is of master at `dab0dbf` (2026-10-12), after Sega Rally (Model 2A) came up and Daytona, Virtua Cop and House of the Dead were prepared. Line numbers are as of `dab0dbf`. Nothing in `src/` was changed by either audit.

Sections 1 to 4 list the open items, ranked by how many games they touch. Section 5 says what became of the first audit, and section 6 lists what was looked at and left alone on purpose.

---

## 1. Every game: the i960 and its interrupts

### 1.1 Call frames live on a host stack, and pfp is never kept

`call`, `callx` and `ret` push and pop `cpu->frame_stack[]` (`i960_exec.h:339-385`, `:1279-1297`, 64 deep, `constants.h:15`). No frame goes to memory, and `flushreg` (`:840`) does nothing. `pfp` is copied from the caller's `pfp`, so it stays 0. The comment says "pfp = old FP", but the code copies the old pfp (`:354-357`; `i960.h:88-92` admits this). Whether a frame is an interrupt's is a host flag (`frame_irq[]`), not pfp's return-status bits.

Three patches follow from this:

- `EMU_IRQ_TABLE_MAX_SLICES` (`emu_thread.h:494`, used at `:705`). The run loop cannot see an SDK kernel's task switch (`flushreg`, a new frame, `bx`), so it decides that a handler "in service" for 8 slices has switched away.
- "Has the handler returned?" is a test of frame depth (`emu_thread.h:510`), not of the PC's priority and interrupted bit.
- A call chain more than 64 deep halts the CPU.

**The board's rule** (MAME `do_call` / `do_ret`) has four parts:

1. pfp is the old FP with return status in bits 0-2.
2. The locals are spilled to memory at FP, and `flushreg` writes the cache out.
3. `ret` dispatches on `pfp & 7`, and 7 restores PC and AC from the interrupt record.
4. "In service" is then the PC's priority and nothing else.

This touches every game. Homebrew task switching is where it bites now, and FV and Sega Rally take table interrupts through it. Check it with `match-replay`, `grade-reset` and `det_digest`. If the instruction counts move, `NETPLAY_PROTO_REV` needs a bump.

### 1.2 STF's interrupts skip the processor's interrupt rules

A profile that names its handlers (`irq_handler[4]`, STF only, `sfight.h:856`) has them entered as a plain call by `hle_interrupt` (`hle_hooks.h:286-294`, `emu_thread.h:457-471`). The priority is not raised, the interrupt stack is not used, and the PC priority mask is not checked. A profile with `irq_vectors` (FV, Sega Rally, homebrew) goes through `hle_interrupt_on_stack`, which follows MAME `take_interrupt`. A profile with neither gets no interrupts at all, silently. m2snake relies on that (`m2snake.h:136-139`).

CLAUDE.md already says STF's own table names exactly the four profile handlers. **Real fix:** every program takes its interrupts from the PRCB table on the interrupt stack, and `irq_handler` stays only as the program-identity check (`profile_program_matches`). This needs §1.1 first, then a re-grade of STF. CLAUDE.md records that a gcc960 handler broke m2-pacman on the old path, so STF's handlers may have been written around it.

### 1.3 The i960's speed is one fitted factor, for one game

Sega Rally's profile sets `i960_cost_permille = 2300` (`srallyc.h:201-209`; `i960.h:109-121`; applied at `emu_thread.h:546`), which charges every instruction at 2.3 times MAME's table. Without it, the car-LOD row chosen from a timer read (0x5BADE0) comes out 8 instead of 5-7, and the third car ahead lags at GO (issue #305). The factor was measured on STF's Model 2B board and is used for a 2A game. While it is set, `m2_spin` and `m2_texload` stand aside.

The board's own costs have since been measured piece by piece (Pinboard #607-#670, `tools/stf_ai/board_bench.py` on PR #283):

- an i-cache line miss costs about 10 counts, and a jump to an uncached line about 21;
- a RAM store costs about 2.1;
- using a load's result at once costs about 3.4;
- a tile RAM store costs about 47;
- a COP FIFO write costs about 12.

A model built from these alone gives a median of 1.004 board/model over 189 STF frames. **Real fix:** charge those costs on every board, then decide grader by grader whether to hold to MAME's table. STF and FV stay on the table only because MAME, their oracle, does. STF's `rand` reads the timers, so the change moves the CPU opponent's choices and every MAME pairing. Expect a large re-grade, and a protocol bump.

### 1.4 Floating-point and divide edge cases

- `fdivr` by zero writes 0.0 "because it happens often with uninitialized geometry" (`i960_exec.h:1040-1052`). `divrl` beside it (`:1105`) divides as MAME does and gives an infinity.
- `fsqrtr` of a negative writes 0.0 (`:1068-1073`, still headed "stubs using math.h"). `sqrtrl` (`:1122`) gives the NaN through `i960_nan_result`.
- The integer divides (`divo`, `divi`, `remo`, `remi`, `modi`, `ediv`, `:697-745`) log an ERROR and leave the destination alone. The chip raises a zero-divide fault; check what MAME does before porting.
- Opcodes with no case run as a NOP after an `UNIMPL` WARN:
  - `calls`, `mark`/`fmark`, `atmod`/`atadd`;
  - the fault-if group (0x18-0x1F);
  - `classr`/`classrl`, `movre`, `cpyrsre`, `logeprl` and `remrl`.

  An unknown MEMB mode gives an address of 0 (`:205-207`). None of these is known to be reached; they are gaps, not patches.

The first two are cheap: make them follow the file's NaN rule (`:76-104`), then run `det_digest`, the netplay check and `match-replay`. `fdivr`'s comment says STF divides by zero often, so this may move frames.

## 2. Every game: board pieces wired up per profile

### 2.1 The sound interrupt waits for an STF RAM address

`emu_sound_pending` (`emu_thread.h:439-446`) returns false unless the profile names `sound_queue_count_addr`, and only STF does (0x504001). So FV, Sega Rally and homebrew never get the TxRDY interrupt (intreq bit 10) this way. The comment just above it gives the board's rule: TxRDY is a level, up whenever the UART can take a byte (MAME `sound_ready_w`, `txrdy && intena`). The RAM read is only there to feed the `queue_hi` statistic.

**Fix:** raise bit 10 from the UART state and `intena` alone, and read the counter only when a profile names it. STF's behaviour does not change. Check Sega Rally's and m2-pacman's sound after it.

### 2.2 Board reset, data window and sound board are set up in each installer

- **Reset from the PRCB.** Only `sfight_install` (`sfight.h:209-226`) starts the CPU as MAME's `device_reset` does: on the interrupt stack, at PC `0x001F2002`. `fvipers_install` (`fvipers.h:179-188`) and `srallyc_install` (`srallyc.h:153-157`) set only IP, so FP is 0.
- **The 0x06000000 data window.** All three installers copy `main_data + 0x1000000` into `xtra_data` (`sfight.h:191-202`, `fvipers.h:166-177`, `srallyc.h:142-151`). That is MAME's `model2_base_mem`, a board rule. sfight.h calls it "STF-specific / found empirically". FV's comment says "from offset 0 (not 0x01000000 as in STF)", but its code uses 0x01000000.
- **`enable_68k_sound`.** This is a profile switch for whether the board has a sound board (`game_profile.h:102`, off by default).
  - STF, m2snake, homebrew and Sega Rally set it.
  - FV does not, so Fighting Vipers runs silent on the same 2B board.
  - HotD sets it false, though its comment says sound.h should take it as it is.

  It should follow from the board variant and an audio CPU ROM being present. Daytona and Virtua Cop have Model 1 sound, which is another board.
- **The videoctl frame bit** (0x0100000C) is served only by the 2A map (`model2a.h:58-66`), whose comment says it is on every Model 2 board. On 2B it is plain storage, so a 2B program that polls it, as Sega Rally does at boot, would hang.

**Fix:** a board-level install that every profile calls. It does the PRCB reset and the data window, sets up the sound board from the ROM set, and serves videoctl on both maps. The three installers then keep only their own work.

### 2.3 The display list goes through the game's model catalogue

`geo3d_scan_geo_list` maps an object's address back to a model-table entry (`geo3d.h:1232-1243`, `geo3d_lookup_by_pol`) and decodes through it (`:1372-1387`). Every profile sets `mesh_ptr_subtract` / `mesh_ptr_add` to the same 0x02000010 / 0x10. That pair is the board's own rule written as constants: the mesh is at polygon ROM `(oba & 0x7FFFFF) * 4` (MAME `geo_object_data`).

Sega Rally has no table and already reads the mesh at the object's address (`game_render.h:2432-2440`, taken whenever `model_idx < 0`). So one board has two draw paths, and only the table path has the mesh cache and the colour from table+4.

`MODEL_ENTRY_SIZE` is fixed at 16 (`geo3d.h:39`), but Daytona's entries are 20 bytes (`daytona.h:74-76`). Every Daytona entry after the first would be misread.

**Fix:** game frames always read at oba. Keep the table for the object viewer, the explorer and labels, with a stride per profile.

### 2.4 The GEO, as the board has it

- **0x802008 reads the GEO's live write position** on the real board: `set_end_mark` takes the list's length from it. This was found when a probe wedged the board (Pinboard #656). We answer the write start (`memory.h:599`), and so does MAME. Nothing breaks today, because STF only reads it right after setting it. A program that reads it mid-list would differ.
- **Z clip.** Three pieces are missing:
  - `zclip_3d` (0x0181C000) is mapped and saved but never read.
  - `master_z_clip` is not modelled.
  - The side planes are a per-window scissor plus the GPU's eye-plane clip.

  `check_culling`'s `max_z < 0` cull is in the direct-data path (`geo3d.h:2182`) but not in the object path. A game that sets its own z clip would draw what the board drops. The projection's near 0.05 and far 20000 (`game_render.h:2332-2354`) stand in for all of this.
- **The frame edge is a profile choice.** STF and FV mark it at `variable_diff_calc` through a hook (deliberate, for MAME pairing; see CLAUDE.md). `board_vblank` profiles mark it at the vblank. `geo_displaylist` (m2snake) publishes all of bufferram at the end of each slice (`emu_thread.h:786-797`, `:819-834`).
  - The board's own edge is the read-pointer write at 0x803008, which `memory.h` already handles for real lists.
  - `board_vblank` only means "no frame hook", so it could be worked out from the hook table.

## 3. The COP's FIFO

- **An unknown command is given 0 arguments** (`sharc_args_for_cmd`, `sharc_exec.h:179`). Its arguments are then read as commands, which has happened twice (`0x2A805555`, `0x17002E2E`). The command word's low byte is the firmware's dispatch index (0x..5151 is dispatch[0x51]). A table of argument counts by index, read from cpres1's PM, would cover every command, including ones not yet seen.
- **Every command empties the reply FIFO** (`sharc_exec.h:679-680`, and `cop.h:184-185` for the stream command). Replies the i960 has not read are dropped, where the board would hand them to the next read. Since an empty read answers 0, this hides a handler that answers too many words. An under-read should be logged at WARN, as an underflow is, not dropped silently.
- **`0x13002626` clamps asin's argument to ±1** (`sharc_exec.h:1450-1458`). `sharc_fw_asin_word`'s own comment (`:407-408`) says the firmware does not clamp (`_L20332`). Delete the clamp and run `cop_replay` and `match-replay`.
- **`0x34006868` (`name_char_kage_disp`, PM 0x205A3) is an empty handler** (`sharc_exec.h:1233-1236`). Whatever DM state it sets for the name-character shadows is missing.
- The short-argument fallbacks that push zeros (`:985`, `:1218` and others) can only be reached from a truncated `cop_replay` input. They are harmless.

## 4. Smaller things

| What | Where | Fix |
|---|---|---|
| The link board's status bit 0 reads back 0 whatever was written | `memory.h:274-288` | Keep the written bit (MAME m2comm). Sega Rally writes 0 and reads it back. |
| `CPU_CTRL` (0xE00000), `UNKNOWN_VID`, `COPRO_STATUS` (0x980014) and `GEO_CTL1` are plain RAM | `memory.h:726`, `:756`; `constants.h:67-70` | Check each against MAME (`ctrl0_w` drives the input mux). Not known to matter. |
| The 68000 takes no exceptions. ILLEGAL halts. TRAP, TRAPV, line A/F, privilege violations and divide by zero only log. CHK, TAS, ABCD, NBCD and SBCD are stubs. | `m68k_exec.h:792-1554` | Vector through the exception table as MAME does. STF's driver hits none of them; another game's driver might. |
| FV's input map is STF's ("bit masks are likely identical"), and it has no coin mapping | `fvipers.h:236-240` | MAME `INPUT_PORTS_START(fvipers)`. |
| `GEO3D_PALETTE_OFF` 0x00100000, "STF-specific", is the palette fallback for every game | `geo3d.h:53` | Make it a profile value or take it from the list. |
| The transparent renderer's coverage threshold, 0.5 of four taps, comes from the explorer | `game_render.h:647` | Check against `model2rd.ipp`. |
| STF's frame counter 0x500020 is hard-coded in the run loop | `emu_thread.h:946` (`sndcap_frame`) | Debug capture only. Make it a profile field or the vblank count. |
| `warning_skip_addr` (STF 0x500410) is poked every slice | `emu_thread.h:236`, `:507` | Kept on purpose (#254): the graders pair frames by content, and a netplay session always skips. |

### The homebrew display-list walker

`geo3d_scan_displaylist` (`geo3d.h:888-1078`) walks m2-snake's own list format (`geo_displaylist`). The other homebrew, m2-pacman, has used the board's GEO walk since #466. This walker carries the most fitted code left in the renderer:

- the near-plane reject `GEO3D_NEAR_CULL` 4.0 (`:628`, `:643-648`), and a line version whose comment says −6 while the code uses 4.0 (`:690-696`);
- the 1.5-unit "degenerate close-quad" reject for HUD elements (`:649-661`), which hides a col0 decode bug;
- an object-level cull at `matrix[11] <= -1.0`, added against starfield streaks (`:999-1003`);
- the flat-colour ×2 boost, "approximating the colorxlat ramp" (`:614-628`, `game_render.h:649-650`);
- the overrides in `game_frame.h:147-165`: flat colour on, because "model 456's ROM material is a meaningless placeholder", and backface culling off.

All of it is gated on `g_geo_flat_color` / `geo_displaylist`, so no game sees it. **Real fix:** put m2-snake's list through the board's walk, as m2-pacman's is, and delete this walker. Failing that, use the clip planes of §2.4 here too.

## 5. What became of the 2026-09-30 audit

| Item | Now |
|---|---|
| The z-sort's patches over the half rule (recede bound, `zsort_standing`, keep-depth, coplanar layers, tie layer, same-matrix runs, held pairs) | **Done (#247, PR #159).** Game frames draw at the board's key (`geo3d_flat_depth`). `zsort_standing`, keep-depth, the tie layer, the runs and the held pairs are gone from `src/`. The round-intro grader (#249/#287) holds all fourteen stages against MAME. There the key is nearer MAME by 539,225 changed pixels to 3,496, and no clipping or bending is left. |
| Mode 0 at the head of a model falls back to the nearest corner | **Done** on the game path: `g_geo3d_flat_prev_z` carries across objects and is reset each frame, as `polygon_z` is. The old fallback is left only in the half rule. |
| Vblank and timers by wait hooks | **Done (#253, PR #163).** The vblank is on the cycle clock, the timers are live, and every wait hook is gone. |
| COP ready bit hooked per game (STF 0xF3C, FV 0x190C) | **Done (#252).** `cop_fifo_status` serves 0x980004 for every game. |
| The COP-stream renderer (shadow floor, eye-bake, camera sign dials, `tgp_bone`) | **Done (#251, PR #162).** Direct data is walked and the fallback is deleted. |
| `SANITIZE` | **Gone (#252).** |
| `0x28805151` pushes zeros | **Ported (#252)**: `Fn_get_glo_ang`. |
| `cop_read` on an empty FIFO | **Logs an underflow at WARN (#252).** It still answers 0, since nothing models the stall. |
| `ldtime` returns 0 | **Returns the cycle count.** |
| `_700000_loop`, `warning_skip_addr`, FV model table, fov | **Settled (#254)**, as recorded in the first audit. |
| `m68k_exec.h` calling its cycle count "approximate" | **Fixed**: it now says it is held to MAME's cycle-level 68000. |
| zclip / clip planes, the homebrew HUD reject | **Still open**: §2.4 and §4. |

### Left over from the half rule

These are not on the game path any more, but they are still in the tree:

- `g_geo3d_zsort_recede` 12.0, `GEO3D_ZSORT_KEY0`, `geo3d_sort_z` and the shader's half-rule substitution (`geo3d.h:323-368`, `:455-464`; `game_render.h:431-436`). These run only for `set_camera {"zflat":0}` and the object viewer.
- The coplanar layers (`geo3d.h:466-533`, `:2256-2707`), with their fitted 0.5 gap, 0.02 tie, cos 0.999, 1% overlap and spiral. Only the object viewer turns them on (`objview.h:324`). The game vertex shader still applies a layer offset, which is always 0 there (`game_render.h:437-439`).
- The `set_camera` keys `zsort`, `zrecede`, `zlayers`, `zlayer_steps` and `zkey0`, which do nothing to a game frame unless `zflat` is 0.

They are kept because the object viewer's free camera has no board sort to copy. There are two choices. One is to keep the `zflat` 0 A/B and leave them. The other is to drop that A/B and move the half rule wholly into `objview.h`.

### Timing lessons from the round-intro grader (still true)

- **A board frame is a vblank, and a game frame can take more than one** (since #163). An input schedule written in board frames drifts against MAME's, so drive inputs by the game's counter (0x500020).
- **The A/V stream tags a display list's picture one board frame early**, and MAME's counter runs one ahead of ours. Measure both; don't assume them.

## 6. Looked at and left alone

These were checked and are deliberate, measured or proven board-identical:

- STF's feature hooks: region, damage, rounds, enemy rank, VS mode, the PS3 cross-play group, the replay stage pin, the `versus_result` observer, the native replay hooks, SKY EYE and the COP-error halt.
- The Console profile's DLL traps and free play.
- The texture-loader row hooks and the idle-spin skip, both held by `det_digest`.
- `--sound-hle`'s fitted costs (`sound_hle.h:110-142`): it is an option for STF's driver only, and the 68000 stays the board.
- Host-side choices, not board ones: the GLES vertex-depth renderer list (`game_render.h:208-243`), the view cull's epsilons and the per-pixel depth option (#304).
- The 60 Hz vblank against MAME's 57.52 Hz (`irq_timer.h:58-63`), which is documented.
- The 315-5649 wheel speed, a feel constant for a digital pad on an analog wheel.
- The host camera's game-RAM reads (`camera_struct_addr`, `camera_angle_addr`), which feed only the free camera.
- The window offsets `84 + H-sync`, `130 + V-sync`, which are MAME's `model2_3d_project`.
- The decal "same diagonal" rule, which matches the explorer. Under the flat key it no longer affects depth.
- These files came out clean:
  - `tgp.h` (a generic MB86234 after MAME; a runaway program stops with a WARN), and `model2a.h` apart from §2.2;
  - `tile_renderer.h`, `scsp.h`, `sound.h` and `m68k_timing.h`;
  - `sharc_coli.h` and `sharc_zanzou.h`;
  - `savestate.h` and `rollback.h`.

## Doc drift found on the way

- CLAUDE.md (HLE Hooks) says FV has its own `co_processor_error_hang` hook. FV's table has only its frame-pace hook.
- `fvipers.h:217-221` still lists `_700000_loop` as a hook to port. It was removed in #254.
- `fvipers.h:166-168`: the data-window comment contradicts its code (§2.2).
- `m2snake.h:18-21` describes the no-frame-hook 60 Hz pacing from before #253.
- `game_profile.h:146-151` says `geo_displaylist` is "required for non-STF games". Sega Rally and m2-pacman use the board's GEO walk.
- The head of `geo3d_scan_displaylist` (`geo3d.h:888-894`) is stale, and so is the line reject's "-6" (`:690`).

## Suggested order

1. **The cheap fixes that move no STF frame.** These are the sound pin (§2.1), the board install (§2.2), the asin clamp and the empty 0x68 handler (§3). For each, run `cop_replay`, `match-replay` and `det_digest`.
2. **The COP argument table by dispatch index, and keeping unread replies (§3).** Both are ROM-free and testable with `cop_test` / `cop_replay`.
3. **The model catalogue out of the game draw (§2.3).** Run `grade-models`, `grade-stages` and `grade-zsort`. This also unblocks Daytona's 20-byte table.
4. **Real call frames, then table interrupts for STF (§1.1, §1.2).** These remove `EMU_IRQ_TABLE_MAX_SLICES` and the STF interrupt path. Re-grade everything and bump the protocol.
5. **Board i960 costs for every game (§1.3).** This has the biggest payoff: Sega Rally loses its factor, and STF's `rand` matches the board. It also has the biggest re-grade, so do it last, once the cost model is final.
