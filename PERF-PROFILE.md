# Where m2hle spends its time (Pinboard #399)

A `perf` profile of master after the spaghetti removal (#181, `c51bf9a`), on x86-64 Linux (the dev
container: WSL2, Ubuntu 24.04, gcc 13, `-O3`). STF, `sfight.zip`, attract. Three runs:

| run | what it is | what it shows |
|---|---|---|
| **bench** | `arc_bench --sound --frames 30000`: 30,000 frames of attract (intro, the replay fight, VS screens, several loops) as fast as it goes, one emu+render thread and the sound thread | the board, and the CPU-side renderer of the handheld / libretro builds (tiles composed on the CPU, sokol's dummy backend) |
| **desktop** | `m2hle --headless --av-port` at 60 fps for 70 s with a client draining the stream, Mesa d3d12 on the RTX 3070 | what a stream or a desktop window costs: tiles on the GPU, the GL driver, the A/V capture |
| **emu only** | `m2hle --headless` with no `--av-port` | the same board with nothing drawn |

`perf stat` over the bench: **3.19 instructions per cycle, 1.18% of branches mispredicted.** The
interpreters are not stalling on their dispatch; the cost is the work itself, so the
way to make anything here cheaper is to do less of it, not to predict it better.

## Per frame

From the bench's own timers (x86, flat out, ~1,100 frames/s): **emulation 0.52 ms, render
0.37 ms** (tiles 0.21, 3D 0.15) on the main thread, plus the sound board on its thread.
At 60 fps on the desktop run, all threads together cost **4.0 ms of CPU a frame**: main
(render + capture + GL driver) 2.3 ms, emu thread 0.95 ms, sound thread 0.71 ms. (The emu
thread costs more paced than flat out: at 60 Hz its caches are cold at every slice.)

## The bench, by subsystem

Inclusive, as a share of all samples on both threads (`perf report --children --inline` on a
`--call-graph dwarf` recording):

| | share | made of |
|---|---:|---|
| **i960 + bus + COP** (main thread) | 32% | interpreter dispatch `i960_step_core` 7.7%, bus `mem_ea` / `mem_fetch2` / `mem_read32` / `mem_write32` ~7%, the COP (`coprogram_write_cb` → `sharc_exec`) 3.5%, GEO list publish ~1% |
| run-loop overhead in `emu_slice_body` | 4.6% | timers, interrupt checks, the slice loop |
| **CPU tile compositor** (handheld/libretro only) | 18% | `s24_draw_line` 4.3%, `s24_cell_load` 2.9%, `tile_cpu_recolour` 2.1%, the colour-line `memcpy` 1.1% |
| **3D, CPU side** | 12% | `geo3d_decode_model_cached` 10.6% with the mesh cache on: the per-face work every frame (`geo3d_cached_face` 7.4%: `geo3d_board_luma` 1.5%, `geo3d_flat_depth` 1.4%, `geo3d_board_normal` 0.8%, cull 0.8%, emit ~1%) |
| **sound board** (own thread) | 31% | 68000 `m68k_step_core` 22% (of which SCSP register reads and writes ~12%), `scsp_sync` 11.6%: DSP program `scsp_dsp_prog_sfight_fvipers` 9.4%, slot PCM `scsp_slot_run_pcm` 7.3% |

## The bench, by inlined function (top 30, exclusive)

`python3 tools/perf_inline.py attract.data arc_bench` — every sample charged to the innermost inlined
function at its address, which `perf report` cannot do (it puts 31% on `emu_slice_body`):

```
  7.66%  i960_step_core                 2.09%  scsp_pcm_wave                  1.37%  geo3d_flat_depth
  6.40%  m68k_step_core                 2.05%  tile_cpu_recolour              1.36%  mem_read32
  4.87%  scsp_slot_run_pcm              2.04%  memcpy                         1.34%  hle_check_synced
  4.76%  scsp_dsp_prog_sfight_fvipers   2.00%  scsp_dsp_unpack                1.21%  emu_timers_after_step_fast
  4.31%  s24_draw_line                  1.87%  scsp_dsp_pack                  1.20%  m68k_rw
  2.91%  s24_cell_load                  1.85%  sound_run                      1.10%  m68k_fetch
  2.85%  mem_ea                         1.70%  m68k_direct                    1.04%  scsp_sync
  2.77%  emu_slice_body                 1.63%  i960_cycle_cost                1.00%  geo3d_cached_face
  2.46%  scsp_slot_env                  1.58%  scsp_ram_w                     0.94%  mem_write32
  2.30%  mem_fetch2                     1.45%  geo3d_board_luma               0.90%  m68k_wait
```

## The desktop run

The main thread is 57% of the process: our code 54% of it, the GL driver stack (NVIDIA's
user-mode driver, Mesa's gallium and d3d12) 32%, libc 11%. Of our part, `game_frame_draw`
(the 3D, the same `geo3d_cached_face` work as above) is the largest, then the A/V capture's
copy of each frame into the stream ring (`av_capture_submit`, 4.2% of the process, only while
streaming), then the tile upload (`video__compose_gpu` → `sg_update_image`, 3.8%). The emu and
sound threads look like the bench's. Loading the zip (`tinfl_decompress`, `mz_crc32`) is about
0.7 s of CPU once, at boot.

## Where to look, if something is to be made cheaper

In order of what it would save, as #399 found it (#403 below took the first two); each has to stay bit-exact
(`det_digest`, `ab-builds`, `arc_bench --draw-digest`).

1. **The 3D's per-face work, every frame** (~12% of the bench, the largest of our own costs on the
   desktop's main thread). The mesh cache keeps the decode, but luma, flat depth, the corner
   normal, the cull and the emit run per face per frame.
2. **The CPU tile compositor** (18% of the bench), for the handheld and libretro builds only.
   #211 already draws only what changed; attract scrolls, so much of it changes.
3. **The i960's per-instruction bookkeeping**: `i960_cycle_cost` 1.6%, `hle_check_synced` 1.3%,
   `emu_timers_after_step_fast` 1.2%. Together about an eighth of the i960's time, paid on every
   instruction whether or not a hook, a timer or a cost table entry has anything to say.
   #1477 took the timer part and the `g_i960_bt_on` test (below); the cost lookup and the hook
   bit are the two inputs that really change with the instruction.
4. **The 68000's SCSP traffic**: register reads that make the slots catch up (`scsp_r16` →
   `scsp_latch_now`, 6%) and writes that sync the chip (`scsp_write`, 8.3%). By design (CLAUDE.md,
   "The SCSP makes its samples late"); STF's driver reads the monitor every ~5 samples.
5. **Copies**: the GEO publish (`geodl_publish`, ~1-2%, texture/polygon RAM beside each list),
   the A/V capture (4%, streaming only).

## What #403 did with it

Measured with `arc_bench` on attract, the box's other load alternated out (base, new, base, new...,
five runs each), and with `perf stat -e instructions:u,cycles:u` for a number that the load does
not move. Every change is bit-exact: `tile_test`, `arc_bench --draw-digest` over 12,000 frames,
and an FNV of the two tile layers' RGBA after every frame (12,000 frames, 4,525 different
pictures), base against new.

| Change | Where | Gain |
|---|---|---|
| `s24_draw_line` walks runs, not pixels: a run stays in one cell, one window-mask group and one side of the split, so the mask bit, the split and the cell key are looked at once per run | tiles | tiles −13% |
| A non-opaque pass decides a cell of the other category is dead from its tile word and never reads its graphics (most cells of the four front passes) | tiles | −1.4G instructions per 4,000 frames |
| A palette change recolours every pixel, with no test: each output pixel already holds its pen's colour, so that is the same bytes, and the per-pixel "did my pen change" branch was the mispredicted one | tiles | tiles −21% with the run walk |
| `geo3d_cached_face` looks up a face's palette colour only once it is drawn, after the cull | 3D | the geo list −23% instructions |

Together: tiles 0.36 → 0.26 ms a frame (−28%), 3D 0.136 → 0.128 ms, render 0.51 → 0.40 ms
(−21%); the whole bench (ROM load included) 55.3G → 45.7G instructions, 16.7G → 14.9G cycles.
On x86. On the handheld, see the next section: tiles 2.40 → 1.98 ms.

What was looked at and left:

- **Skipping the SCSP DSP while its input is silent.** The input (`mixs`) is zero in nearly every
  sample, but the reverb never settles: once the first sound has gone in, the delay line sits in a
  limit cycle that neither decays nor repeats (about 10,000 of its 32,768 words change every
  revolution, `efreg` stays at −1, −1). Only the samples before the first input are skippable
  exactly: 30% of attract, 55% of a scripted fight, all of it in a session's first minutes.
- **Idling the 68000.** STF's driver polls the slot monitor, which changes every sample, so its
  wait loop is never idle. `--sound-hle` already runs that driver in C.
- **The i960's per-instruction bookkeeping.** `i960_cycle_cost` is one table load and
  `hle_check_synced` one bitmap test; what is left is the interpreter itself, which #187 and
  #294 have been over. The texture loader and the spin skip read the cycle accounting, so a
  change there is a board change, not a host one.
- **The zkey of a culled face.** `geo3d_flat_depth` has to run for every face (a culled face
  still sets the previous z), and the key itself is a few integer operations.

## What #1477 did with the per-instruction bookkeeping

Of what the fast path looked at after each instruction, only the opcode (its cost) and the IP
(the hook filter's bit) change with the instruction. The rest changes only at events:

- `g_i960_bt_on` is fixed for a slice (only daytona, srallyc and vcop set it). The loop reads it
  once and hands it to `i960_step_core`; a global was reloaded after every store.
- The timers: `pending` was a load, an add and a store an instruction, then the horizon test and
  the `intreq & intena` test two loads more. Now the loop stores the boundary and compares it with
  one precomputed cycle count, `g_irqt_due`, where pending reaches the horizon (or 0 while a
  board interrupt is up), and `pending` is brought up only when something reads it
  (memory-bus.md has the rule). The attention word is re-read only when that work ran.

Attract (`arc_bench --sound --frames 3000`, `perf stat -e instructions:u`, the whole process):
50.395G → 49.247G instructions, −2.3%, the same to 30K over two runs; the sound thread does not
change, so it is all the i960's. Bit-exact: `det_digest` frames and sound hashes on attract (3000)
and the scripted fight (4500), `ab-builds --sound` at 600/1800/3000 on sfight and at 600/1800
on the board-timing games.

## What #406/#408 did with it, and the handheld

**The GEO publish copies only what changed** (`geodl_publish`, `geo_raster_publish` in
`memory.h`). Each published list used to be a copy of all 512 KB of bufferram, and texture,
polygon and log RAM were copied whole whenever a list had written any of them: STF rewrites the
eyes' texture points and the material slots every frame, so that was ~290 KB a frame for a few
hundred bytes of change. Now the list walk that applies the state copies the words it visits
(the renderer's walk takes the same steps and reads nothing else), and a write marks its
256-byte block dirty for each published copy. A profile in the homebrew list format
(`quirks.geo_displaylist`) still gets all of bufferram. `dump_geo_list` writes the published
snapshot, so outside the list's own words it now holds older frames' bytes.

Exact: the published texture, polygon and log RAM against live after every publish, and the
renderer's walk over the partial copy against a full one, 0 mismatches over 12,000 frames;
`--draw-digest` identical over 12,000 frames of STF and 8,000 of Fighting Vipers; `det_digest
--cpu` identical over 9,000. x86: 2.4% fewer cycles a frame.

**On the RG ARC-S** (RK3566, Cortex-A55), `arc_bench` cross-built (`-mcpu=cortex-a55`), 3,000
frames of attract, the clock pinned at 1416 MHz so throttling cannot pick a winner, the three
builds alternated four times, each run started below 57 C. The four runs of a build agreed to
0.02 ms:

| Build | emu ms | render ms | tiles ms | 3D ms | frame ms |
|---|---|---|---|---|---|
| master before #403 (d641b75) | 3.36 | 3.86 | 2.40 | 1.35 | 7.22 |
| #403 (3c0428e) | 3.36 | 3.42 | 1.98 | 1.34 | 6.78 |
| #403 + GEO publish | 3.12 | 3.42 | 1.98 | 1.34 | 6.54 |

So on the device #403's tile work is −17.5% tiles and −11% render (it was −28% / −21% on x86),
its 3D change does not show, and the GEO publish is −7% of the emu thread. Together −9.4% of a
frame's CPU, 138 → 152 frames a second flat out.

## What the Dreamcast branch has for master (#460)

The Dreamcast port (`idea-340-dreamcast`, DREAMCAST-PORT.md there) was optimised hard for a 200 MHz
SH-4. Most of it is the SH-4's or the PowerVR's: the store queues, `ftrv`, `FSRRA`, the software
pager, the model pack, ADX sound, drawing every other frame. What is board code was measured here on
x86 with `det_digest` over 9,000 frames (attract, then a coin, a start and a punch every 30 frames
into a fight), every variant byte-identical to its base with `--cpu`:

| Dreamcast change | x86 result | for master? |
|---|---|---|
| `MEM_COUNT=0`: no 64-bit bus tallies | instructions −0.2%, cycles unchanged | no: a 64-bit add is one instruction here |
| `MEM_LE_DIRECT`, `IRQT_COUNT_T=int32_t` | GCC and clang already fold the byte loads on x86 and ARM; 64-bit adds are free | no: SH-4 only |
| `I960_BLOCKS`, the decoded-block cache | +1.1% time | no |
| `I960_OPTABLE`, handler-table dispatch | −0.3% time | no |
| `I960_AOT`, the ROM compiled to C (cover 0.99) | −1.1% time; 14% of i960 instructions compiled | no: little gain, and the generated C holds ROM words, so a CI-built core could not ship it |
| `m2_spin_timed`, the texture senders' timer-2 wait skipped | ~15,000 passes in 9,000 frames, instructions −0.007% | no: it mattered there because each pass ended a compiled run |
| tile dirty bits, publishing only the GEO state that changed | master already has both | done |
| the audit's fixes (#458): Gems traps kept off profile hooks, Gems' NaNs canonical, bufferram bounds that wrapped | correctness | yes, ported |

On x86 the i960 is not where the time goes: the 9,000 frames are 228 G instructions, most of them
the sound board and the render, so even the AOT's whole gain is about 1%. The handheld's profile
(above) has the same order, sound board and draw ahead of the i960.

## 2026-10-22: the most expensive functions, again (Pinboard #1474)

The same bench (`arc_bench --sound --frames 30000`) on master at `4075c17`, after everything above,
re-profiled to answer "what is the most expensive function, and is it the SCSP". The box was shared
(load average ~9), so wall-clock times below are rough; shares of samples and instruction counts
(`perf stat -e instructions:u`) are not.

**By thread:** main (i960, bus, COP, 3D decode, CPU tiles, render) 67.5%, the sound thread 32.5%.
Per the bench's own timers, flat out: emulation 0.40 ms and render 0.38 ms a frame on the main
thread (tiles 0.24, 3D 0.12), the sound board about a third again on its own thread. `snd_bench`
puts the sound board alone at **~49x real time** (60 s of sound in 1.22 s), i.e. ~2% of a core at
60 fps; the live thread agrees.

**By real function** (`perf report --sort symbol`, inlining folded in): `emu_slice_body` 28.4% (the
whole i960 + bus + COP + timers), **`scsp_dsp_prog_sfight_fvipers` 9.6%** (STF's reverb, the single
most expensive function of the process), `scsp_slot_run_body` 7.4%, `m68k_step_core` 6.3%, the GEO
list walk 10.4% in pieces, the CPU tile compositor 6.9%, `render_frame` 4.75%, `mem_ea` 3.6%.

**By inlined function** (`tools/perf_inline.py`, top of 39,237 samples):

```
 10.30%  i960_step_core                 2.26%  scsp_slot_env                  1.52%  hle_check_synced
  6.52%  m68k_step_core                 1.88%  s24_draw_line                  1.49%  geo3d_emit_tri_uv
  4.23%  scsp_dsp_prog_sfight_fvipers   1.82%  geo3d_board_luma               1.48%  mem_read32
  3.83%  scsp_slot_run_pcm              1.80%  scsp_pcm_wave                  1.29%  m68k_direct
  3.74%  s24_cell_load                  1.79%  scsp_dsp_pack                  1.15%  sound_step
  3.62%  memcpy                         1.74%  scsp_dsp_unpack                1.14%  tile_compose_cpu
  3.15%  emu_slice_body                 1.65%  geo3d_flat_depth               1.05%  mem_write32
  2.95%  mem_ea                         1.59%  emu_timers_after_step_fast     1.03%  geo3d_board_normal
  2.59%  mem_fetch2                     1.58%  scsp_ram_w                     0.99%  m68k_wait
```

So the hunch was right: the sound thread's 32.5% is the DSP program ~10% (the program body 4.2%,
its 23 float16 unpacks and 10 packs 3.5%, its 33 delay-line RAM accesses 1.6%, the 24-bit sign
extensions 0.7%), the slots ~8% (`scsp_slot_run_pcm` + env + wave), and the 68000 ~12.5%
(`m68k_step_core`, its bus, the two wait states, the SCSP register reads and writes). The rest is
`scsp_sync` and the thread's hand-over.

**Is the lazy chip misbehaving?** No. `det_digest` now prints the chip's counters (`sound: chip syncs,
single-slot catch-ups, lockstep samples`): attract (3000 frames, 2.2 M samples) has one full sync
per ~108 samples, 0.69 single-slot catch-ups a sample and **1** sample made in lockstep; a scripted
fight (4500 frames, 3.3 M samples) one sync per ~81 samples, 0.68 catch-ups a sample and 896
lockstep samples (FM voices). The fallback paths are not where the time goes; the cost is the
chip's work itself at 44.1 kHz.

**Where the DSP's time goes.** `perf annotate` of the program shows no hot spot: the cost is spread
evenly over ~3,000 straight-line instructions a sample (84 steps, ~36 instructions each, with the
multiply, the two clamps, the temp/mems traffic and, on 33 steps, the float16 and the RAM access).
At ~190-200 ns a sample it runs at ~3 IPC; it is throughput-bound on instruction count, so a gain
has to be fewer instructions. A silence skip is impossible (the delay line's limit cycle, SCSP.md),
the unpack LUT and the branchless unpack were measured and rejected before (`scsp.h`, above `scsp_dsp_pack`).

**What this did:** `tools/gen-scsp-dsp.py` now checks the program's delay ring against the end of
sound RAM **once a sample** (`ring_ok`: RBP, RBL and the widest address the program forms) and
makes the 33 accesses plain big-endian word reads and writes inside it (`scsp_dsp_ring_rd/wr` in
the generated header), instead of `scsp_ram_w/ww` with its mask and bounds test on each. A ring
past the end of RAM takes the checked path as before. Bit-exact: `snd_bench` hashes (out
`3704855bb1f0b17b`, ram `ee38c9db48fd2123`, efreg `3dcf915d389ad344`, dsp-state
`8caaee08bcbdff0f`), `det_digest --sound` digests and sound hashes for attract and the fight,
`scsp_dsp_test`, `scsp_dsp_test_masks` and `scsp_lazy_test` all unchanged. Gain: the DSP's own
instructions **−3.5%** a sample (12.72 G → 12.29 G over 4 M samples), `snd_bench`'s DSP time
201 → 185 ns a sample (noisy box), the whole board's instructions −0.3% over 3000 frames. Small,
as expected of a function already at its ceiling; the next real step for the sound thread would be
a different design (the 68000 in threaded code, or the driver in C, `--sound-hle`, which #173 has),
not more peephole work on the chip.

**The 68000's share** is the dispatch itself: the hottest instructions of `m68k_step_core` are the
`m68k_fast` table lookup and its indirect jump, i.e. one mispredicted jump per instruction. That is
the price of a table interpreter; the handler bodies are cheap.

**What was left alone, and why:** `scsp_slot_run_pcm` is already the slot's registers hoisted out of
the loop; its hot instructions are the pitch-LFO multiply and the stack spills of its flags, each
~1-2%, nothing to take in one piece. `scsp_eg_update` is a few adds. The i960 is not the sound
thread's problem and was covered by #403/#406.

## How to take one

`perf` comes from `linux-tools-generic` (in the antigravity-dev image; `/usr/local/bin/perf`).
Build with symbols and frame pointers, at the release optimisation:

```bash
cmake -S . -B $B -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="-g -fno-omit-frame-pointer" -DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer"
cmake --build $B -j6
# arc_bench is not a CMake target; det_digest's flags build it:
gcc -g -fno-omit-frame-pointer -O3 -DNDEBUG -std=gnu11 -ffp-contract=off -fno-strict-aliasing \
    -DM2HLE_BUILD_FLAVOR=\"release\" -DM2HLE_VERSION=\"dev\" -DM2HLE_DEV_TOOLS=0 -DM2HLE_AGES \
    $(grep C_INCLUDES $B/CMakeFiles/det_digest.dir/flags.make | cut -d= -f2-) \
    tests/arc_bench.c -o $B/arc_bench $B/libminiz.a -lpthread -ldl -lm
# a Ninja tree has no flags.make; the include list is in build.ninja instead:
#   grep -A6 'build CMakeFiles/det_digest.dir/tests/det_digest.c.o' $B/build.ninja | grep ' INCLUDES = ' | sed 's/^ *INCLUDES = //'
```

`snd_bench` and `snd_replay` want a MAME sound capture (`tools/mame/snd_capture.py`, tools/README.md).
In the dev container the shared MAME build makes one in a few minutes: `MAME_EXE=~/build/mame-bin/mame-shared/shared`,
`MAME_ROMPATH=$ROMS_DIR`, `MAME_EXTRA_ARGS="-video none -debugger none"`, `SDL_VIDEODRIVER=dummy`, and
`BRIDGE_PORT=<free port>` when the bridge's 6510 is taken. Hold any change to the chip against its hashes
(`snd_bench <cap> 60 400000`) and against `det_digest --sound` before and after.

Then, from a scratch directory (never the ROM folder):

```bash
perf record -F 2000 --call-graph fp -o attract.data -- $B/arc_bench $ROMS_DIR/sfight.zip --sound --frames 30000
python3 tools/perf_inline.py attract.data $B/arc_bench --top 40          # by inlined function
perf report -i attract.data --no-children --sort srcline --stdio -g none   # by source line
# inclusive, through the inlining: a dwarf unwind (big: 170 MB for 12,000 frames at 500 Hz)
perf record -F 500 --call-graph dwarf,16384 -o dwarf.data -- $B/arc_bench ... --frames 12000
perf report -i dwarf.data --children --inline --sort symbol --stdio -g none
python3 tools/perf_inline.py dwarf.data $B/arc_bench --callers __memcpy_avx_unaligned_erms
```

The desktop path: `GALLIUM_DRIVER=d3d12 perf record ... -- timeout -s INT 70 $B/Release/m2hle
--headless --rom $ROMS_DIR/sfight.zip --profile sfight --run --no-nvram --av-port 39399
--av-size 496x384`, with a client reading the port (any socket that reads and discards);
without a client nothing is drawn.
