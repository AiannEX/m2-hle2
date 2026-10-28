# The SCSP: what it does, and how this emulator runs it

The Model 2 sound board is a 68000, 512 KB of sound RAM, the sample ROMs and a
Yamaha YMF292 "SCSP" (the Saturn's sound chip). The 68000 runs the game's own
driver (`epr-19021.31` for STF, a disassembly is in
`schamp_SoundDriver_disasm/`), and the SCSP makes the sound. This page is the
chip as `src/board/scsp.h` models it, what STF's driver uses of it (measured),
and why the emulator runs it the way it does (Pinboard #166, "SCSP HLE").

## What the chip is

Four blocks share sound RAM with the 68000.

**32 slots (voices).** Each slot plays one sample out of sound RAM:

- *Source*: 8- or 16-bit PCM from a start address SA (`SSCTL` 0), or the noise
  generator (1), or silence (2, 3). Sample bits can be inverted (`SBCTL`).
- *Pitch*: an octave and a 10-bit fraction make a 20.12 fixed-point step per
  output sample; the pitch LFO (`PLFOS`/`PLFOWS`) multiplies it. Reads are
  linearly interpolated between the two samples around the position (MAME's
  choice).
- *Loop*: none, forward, reverse or alternating, between LSA and LEA
  (`LPCTL`). A voice with no loop ends at the later of the two; that clears
  its KYONB bit.
- *FM*: a slot can add the average of two entries of the "sound stack" (the
  last 64 slot outputs) to its read position (`MDL`, `MDXSL`, `MDYSL`).
- *Envelope*: attack, decay 1, decay 2, release (`AR`, `D1R`, `D2R`, `RR`,
  key-rate scaling `KRS`, decay level `DL`), then the amplitude LFO and the
  total level `TL`.
- *Out*: three sends from the one sample. The direct mix (`DISDL`, `DIPAN`),
  the DSP's input bus MIXS[`ISEL`] (`IMXL`), and the sound stack.

Key-on is two-step: a slot's KYONB bit says what it wants, and a write with
KYONEX set commits every slot's KYONB at once.

**The DSP.** A 128-step microprogram (MPRO) runs once per sample over 16 MIXS
inputs, two EXTS inputs, 128 TEMP registers and a delay line in sound RAM
(RBP, RBL; 16-bit floats in MAME's format), and writes 16 EFREG outputs.
Slots 0-15's `EFSDL`/`EFPAN` put EFREG n in the mix. It is a programmable
effects processor; games load a reverb.

**The mixer.** Direct sends plus EFREG, clamped (18- or 16-bit DAC), times the
master volume `MVOL`.

**The control block.** Three timers (A, B, C) counting in samples scaled by a
prescaler, an interrupt controller that raises one of seven 68000 levels per
source (`SCILV0-2`, `SCIEB`, `SCIPD`, `SCIRE`), a MIDI input FIFO (the i960's
commands arrive here, off the i8251 serial line) and a MIDI output, the slot
monitor (0x408: `MSLC` selects a slot, the chip latches its play position CA,
envelope state SGC and level EG once a sample), and a DMA engine between sound
RAM and the registers.

## What STF's driver uses of it

Measured over 90 s of attract and 200 s of a scripted one-player game
(slot registers sampled at every sample boundary, 2026-09-30):

| | attract | game |
|---|---|---|
| voices sounding, mean / max | 16.4 / 32 | 18.3 / 32 |
| 8-bit samples | 100% | 100% |
| forward loop / no loop | 76% / 24% | 67% / 33% |
| reverse, alternating loops, FM, noise, SDIR | none | none |
| pitch LFO / amplitude LFO | none | 0.3% / none |
| sending to the DSP (`IMXL` ≠ 0) | none | 0.1% |
| slots 0-15 mixing EFREG back in | 10% | 9% |

- **Every voice streams.** The driver keys a voice with LSA 0 and LEA 0x1FFF
  (key-on at `0x601F0A`: `move.l #$1FFF,4(a5,d4.w)`): an 8 KB window in sound RAM,
  played as a forward loop, whose half the chip is not playing the driver
  refills from the sample ROMs. To know which half that is, it writes the slot
  number to MSLC, waits ten `ror.l`, and tests CA bit 0 in 0x409. That poll is
  most of what the driver does: MSLC is written 0.9 times a sample, the monitor
  read once every five.
- **The reverb is per instrument.** The DSP send byte (slot + 0x15, ISEL/IMXL)
  comes from the instrument definition (`move.b $140D(a6),$15(a5,d4.w)`,
  InsData + 0x0D), and almost no instrument sets it. The DSP still runs its 84
  steps every sample: it is the biggest single cost on the sound board (~36%),
  and in attract its input is silence.
- **Timers are the tempo.** Timer B and C are reloaded inside their interrupt
  handlers (~850 times a second each), timer A ~85 times; the time the handler
  takes before the reload is part of every period (CLAUDE.md, "Timing").
- **The delay line** is 64 KB at 0x70000-0x7FFFF, a page of its own.

## How the emulator runs it: the chip's own time

The chip used to run in strict lockstep: after every 256 clocks of the 68000,
one call made one sample of all 32 slots, the DSP and the mix. That is exact,
and it is the most expensive way to be exact.

What the driver can observe is narrow. It reads the monitor, a slot's KYONB,
the sound stack and the DSP's registers, and it shares sound RAM with the
slots (which read it) and the DSP (which reads and writes it). Everything else
the chip does is the output. So `scsp.h` now makes its samples late
("The chip's own time"):

- The 68000 loop only says a sample boundary passed (`scsp_tick`).
- Each slot keeps its own place in the samples owed, and is run on alone,
  a stretch at a time in a tight loop (`scsp_slot_run_pcm`; in release or a
  held decay 2, `scsp_pcm_run_eg`, PERF-PROFILE.md #1482), when something
  looks at it: its KYONB, the monitor if it is the slot MSLC named at the last
  boundary, or a 68000 write inside the RAM it may read. The driver's refill
  writes run one slot on; its monitor poll runs one slot on; neither makes the
  chip catch up.
- A full sync (`scsp_sync`: every slot up to now, then the DSP and the mix
  sample by sample) happens at the end of each run and before any register
  write that changes what the chip makes. Timer, interrupt, MIDI and MSLC
  writes do not sync.
- Slot contributions to each owed sample are integer sums, so the order the
  slots arrive in does not matter: the output is bit for bit the lockstep
  chip's.
- FM, the noise generator, or a voice reading the DSP's delay line make the
  chip `coupled`, and every boundary is then a full sync: the lockstep chip.

In attract that is one full sync per ~90 samples and 0.7 single-slot catch-ups
per sample. The sound board is **~10% faster** (60 s of attract audio, best of
8 interleaved: 1.52 s → 1.37 s on x86; the voices went from 1.42 G to 0.97 G
instructions), with the same output and RAM, every sample.

This is not the high-level sound engine that was rejected before (CLAUDE.md,
"Do not go back to a key-on event mixer"): nothing is guessed. Every value the
driver can read is made exactly when it reads it.

### Why it is exact, and how that is checked

A slot run alone is only right if nothing it reads changes under it. The RAM a
voice may read before its registers next change is bounded in
`scsp_slot_range`, and everything not provably bounded claims all of RAM. Two
MAME behaviours make that less obvious than it looks: a forward loop shorter
than one pitch step lands past its end again and creeps on through RAM, and
the alternating loop reads a position that stepped below 0 as past LEA and
reflects it far beyond the loop.

A forward loop longer than one step is the exception that gets no margin: both
positions the voice keeps (`cur`, `nxt`) wrap at LEA before the next read, so
it reads no higher than `max(LEA - 1, cur, nxt)`, the last two for a position
a register write left past LEA. House of the Dead, Dynamite Cop and Motor Raid
keep an 8 KB voice at 0x6E000-0x6FFFF, just below the DSP ring at 0x70000;
with the old margin of `2 * adv + 3` samples its range ran 4 bytes into the
ring and put 55-61% of their samples in lockstep (#1713). Now none.


- `tests/scsp_lazy_test.c` (ctest) runs random voices and DSP programs twice
  with different garbage everywhere outside the RAM they claim. It found both
  traps above; ten planted range bugs each fail it.
- `scsp_fuzz` holds the chip against the lockstep one under random register
  traffic, with scenarios that leave the chip alone for long stretches.
- `m68k_fuzz` (random code on the whole board) at 1, 37 and 735 samples a run,
  `det_digest --sound` over 100 s of attract and a 12,000-frame scripted game
  (sound thread on and off), and `snd_bench`'s hashes: all identical to master.
  The fuzz also matches between GCC and emcc (wasm).

### What is left

The DSP was the largest single cost, and in STF it mostly reverberates
silence. There is no exact shortcut: integer feedback has no zero fixed point,
so the delay line and TEMP never settle, they only get small. The quiet DSP
below skips it once they are, within a measured margin.

## A quiet DSP (`--scsp-exact` for the exact chip)

Measured on the 90 s attract capture (`snd_bench` with counters, Pinboard
#1681): the DSP's inputs (every MIXS) are zero on 99.8% of samples; EFREG is
all zero on 69% and within ±2 on 92.6%; the longest run of zero input is 2.98 M
samples (68 s). The reverb grinds a 24-bit tail round its delay line for the
whole of it, and that tail is below one output LSB.

`scsp_sample_mix` therefore asks `scsp_dsp_due` before each step. The DSP is a
filter over its own state -- TEMP, MEMS, EFREG and the ring `[RBP, RBP + RBL)`
of sound RAM -- and its inputs MIXS and EXTS. Once the inputs have been zero
for 128 samples, `scsp_dsp_quiet` scans that state: every TEMP, MEMS and ring
value within `SCSP_DSP_QUIET` (512, 24-bit units) and every EFREG within
`SCSP_DSP_QUIET_EF` (2). A stable program can only make outputs that small
from a state that small, so the chip stops running it: EFREG stays as it was,
DEC stays, the ring stays. It runs again on the first nonzero MIXS or EXTS, on
any DSP register write (COEF, MADRS, MPRO, RBP/RBL) and on a 68000 write into
the ring (`scsp_ram_touch`). A program with a TABLE tap can read anywhere in
RAM and is never skipped; nor is the DSP while the chip is `coupled` (a slot
playing the ring as PCM). A scan that finds the state loud is not repeated
for RBL samples, so the scan costs at most one unpack per sample while the
tail is still decaying, and nothing once the DSP is stopped. The three fields
it needs (`dsp_frozen`, `dsp_zero_run`, `dsp_check_in`) sit in what was
padding in `scsp_t`, so every savestate's `LAYOUT` still matches and a state
from before loads (checked: a state `det_digest --save-at` wrote at frame 1200
on master loads here and runs on identically).

**The margin, measured** (`tools/scsp-ab.py`, the exact chip's WAV against
the quiet one's, 90 s of attract through `snd_replay`):

| | exact chip | quiet DSP |
|---|---|---|
| DSP samples run | 3,969,000 | 354,908 (91.1% skipped) |
| `snd_bench`, 90 s of board | 1.79 s | 1.19 s (−34%) |
| output frames that differ | | 228,738 (5.8%), every one by 1 LSB |
| max \|diff\| / RMS diff | | 1 LSB / 0.17 LSB, SNR 72.2 dB |
| difference's spectrum, all bands | | −103 dBFS (exact −28.7 dBFS); worst band 16-22 kHz at 51 dB under it |
| level shift of any bin above −90 dBFS | | ≤ 0.002 dB |

The 1-LSB frames are the exact chip's −1 tail where the frozen EFREG gives 0.

**What the 68000 and the i960 can see.** The DSP's registers read back
(0xC00-0xEFF) and the ring's bytes keep the frozen values instead of the
decaying ones; no Sega driver reads either (STF's never touches the DSP's
RAM range: 0 reads, 0 writes in 90 s, `scsp_ram_touch` counted). So the
i960 sees nothing: `det_digest` over 5400 frames of attract and a 6000-frame
scripted match has every i960 column and the sample clock identical to
master, and only the sound-RAM hash differs (the ring). Not a
`NETPLAY_PROTO_REV` bump. `scsp_fuzz` differs from master only in the DEC it
dumps.

**The exact chip is one switch away**: `--scsp-exact` (m2hle, `det_digest`),
`M2HLE_SCSP_EXACT=1`, `SCSP_EXACT=1` for `snd_replay`, `snd_bench`, `scsp_fuzz`
and `m68k_fuzz`. With it every output of this build is `cmp`-identical to
master's (replay `.bin`/`.ram.bin`/`.regs.bin`/`.wav`, both fuzzes, `det_digest
--sound` with the sound-RAM hash). Grade against MAME and hold two builds to the same
bits in that mode; measure the margin of a change to the quiet path with
`scsp-ab.py`.

## When it isn't quiet: the voice loop (Pinboard #1698)

In a fight the DSP still skips 94% of its samples, and what the SCSP costs is
the voices: `scsp_slot_run_pcm` and `scsp_sync`'s mix. Three changes, all
exact (integer sums in another order, and an identity), cut them:

- **Interpolation** inside the loop: `(a (4096 - f) + b f) >> 12` is
  `a + ((b - a) f >> 12)` to the bit, with one multiply fewer.
- **A voice that sends nothing to the DSP** (IMXL 0: 99.98% of STF's voice
  samples) gets its own `scsp_pcm_run_eg` variant without the MIXS add. That
  function now makes the run's samples into a buffer first, then mixes it
  into L, R (and MIXS) in loops the compiler vectorises; the sound stack
  (`sous`) is written only for the run's last even and odd samples, which are
  all a later read can see (FM couples the chip, and the 0x600 read catches
  up first).
- **EFREG's return to L/R** (`scsp_fx_t`) is listed once per sync (the slots
  with EFSDL) and summed again only after a DSP step: a frozen DSP costs
  nothing at the mix.

Measured on a 6000-frame scripted match (`det_digest --sound`,
`perf record -e cycles:u`): `scsp_slot_run_pcm` 14,111 → 11,207 samples
(−21%), `scsp_sync` 3,181 → 2,727 (−14%), the SCSP as a whole −17%; the DSP
is unchanged. `snd_bench` over 90 s of attract: 1.18 → 1.15 s, and 1.79 →
1.71 s with the exact chip. Bit-exact with and without `--scsp-exact`:
`snd_replay` (`.bin`/`.ram.bin`/`.regs.bin`/`.wav`), `scsp_fuzz`,
`m68k_fuzz`, `snd_bench`'s hashes and `det_digest --sound` (attract and the
match, every column and the sound-RAM hash) are `cmp`-identical to master.

Looked at and left: skipping inaudible voices (TL/EG under one LSB) is ~2%
of voice samples; the loud reverb's 84 steps all feed EF0/EF1 through the ring
and TEMP, so no step is dead.

## The driver in C (`--sound-hle`)

`src/board/sound_hle.h` is STF's sound driver ported to C, run in place of the
68000 when the host asks for it (`--sound-hle`, `M2HLE_SOUND_HLE=1`, the
libretro option "Sound driver"; Pinboard #173). Off by default: the 68000 stays
the board, the oracle, and the only path for any other program ROM. It takes
the builds of the driver it knows, by a hash of their code (`k_shle_drvs`,
"Other games' builds" below) -- m2-pacman talks back over MIDI out, and
nothing but the 68000 answers that.

**What it is.** The driver, routine by routine, from the IDA listing in
`schamp_SoundDriver_disasm`: MIDI framing into the command queue, the command
dispatcher (songs, effects, the `A? 0x` specials), the music and effect
sequencers, voice allocation and stealing (the 5-voice effect cap, priorities,
exclusive groups), the controllers, the fades, the release countdowns, and the
streaming refill of each voice's 8 KB window. It keeps the driver's own RAM
layout, so a sound capture's RAM dumps read the same on both paths, and it
writes the chip and sound RAM through the same paths the 68000's bus takes.
**The chip is scsp.h either way**: what it plays is the same bytes and the
same register values, only at slightly different moments.

**What it cannot be: exact.** The 68000's main loop spreads the work out -- one
queued event per pass over the slots, a streaming refill in between, a
preloaded sample set copied a byte at a time for a third of a second -- and
the driver's timers run a little later the busier it is, because it reloads
them in the interrupt handler. The port models the parts that move notes by
more than a millisecond, each measured on the board with `snd_replay`'s
`$SND_TRACE`: the main loop's time per event and per preload byte, the
handlers' lengths and how they block each other, the extra interrupt latency a
refill's `movem.l` causes, the boot and restart timeline (the driver ignores
the MIDI line for 2.94 s, and InitSCSP's four reads of MIBUF throw away what
waited), and when a streamed one-shot is switched to its end (which is when its
voice is freed, and so what the effect cap counts).

**The i960 cannot tell.** It sees the sound board only through the UART, and
the UART is clocked by board time, which runs the same with or without the
68000 (`g_sound.m68k.cpu.cycles`, 256 a sample). The only way the two could
part is a slice that sends more than ~50 bytes, where the run-ahead cap decides
when a byte waits, and STF never does. `det_digest --sound-hle` against the
68000: every frame identical (frame check, work RAM, buffer RAM, COP memory)
over 12,000 frames of attract and 20,000 of a scripted game with 793 sound
commands, no byte ever waiting a slice. So it is safe in a netplay session,
even against a machine on the 68000.

**Graded against the board** (`tools/grade-sound-hle.py`, `snd_replay` on
`tools/snd_stimuli.py`'s inputs, 120 s each; the key-on is matched on what it
plays -- pitch, level, pan, release, LFO -- not on the slot):

| input | board key-ons | matched within 30 ms | timing, median / p95 | hold p90, board / C | loudness C/board | envelope corr. | bands, dB |
|---|---|---|---|---|---|---|---|
| bgm (every song) | 1991 | 100.0% | -0.6 / 4.1 ms | 1.484 / 1.481 s | 0.986-1.003 | 0.919 | within ±0.03 |
| sfx (effects storm) | 7435 | 99.9% | -5.3 / 8.4 ms | 0.729 / 0.721 s | 0.989-1.011 | 0.833 | within ±0.02 |
| sys (every AE 14, random commands) | 3480 | 99.9% | -3.2 / 19.4 ms | 0.848 / 0.850 s | 0.971-1.025 | 0.859 | within ±0.11 |
| fuzz (random bytes, restarts) | 159 | 100.0% | -2.9 / 5.2 ms | 8.279 / 8.279 s | 0.982-1.020 | 0.989 | within ±0.01 |

And over 5 minutes of the scripted game (the i960 driving it): loudness within
3% and every band within ±0.5 dB in every 20 s window. The envelope correlation
figures are 5 ms envelopes, which a few milliseconds of timing and the odd
voice taken from a different note move; the notes themselves are the board's.

**Cost.** The sound board takes about half the time: 120 s of the music input
in 1.3 s against 2.4 s on x86 (`snd_bench`, `SND_HLE=1`), and on the RG ARC-S's
Cortex-A55 60 s of it in 4.40 s against 8.21 s (effects storm 5.36 against
9.11). What is left is the chip, and the DSP is most of that: in STF's music
its return is exactly zero (measured by muting it), which the DSP shortcut
under "What is left" would take.

### Other games' builds (Pinboard #1737)

STF's driver is one build of a driver that Sega used across Model 2: the same
code layout, the game's data behind the pointer block at 0x608000. The port
takes eight builds, each a `shle_drv_t` (where its own tables sit in the code,
the bank bits it ORs into the sound control register, and what changed):

- **sfight** (Sonic the Fighters; Fighting Vipers is the same code),
  **skytargt**, **von** and **gunblade**: STF's tables, von and gunblade with
  bank bit 0x20.
- **vcop2**: the restart (MIDI 0xFF) keeps sound RAM and the DSP program.
- **doa**, **airwlkrs** (and Behind Enemy Lines, same code): a 512-event queue
  with a word count, each full-queue test against its own value; "everything
  stops" empties the queue.
- **manxtt**: the engine (A?7x) records are 0x12 bytes with a volume cut
  (A?76), the sends are re-evaluated after A?72..76, DISDL 0xC0, the engine's
  effect scan is capped at byte_60807C, and a stray MIDI data byte is dropped.

None of them writes MIDI out (every 0x405 in their code is a MIBUF read), so,
as for STF, the i960 sees only the UART and cannot tell.

Grading another game: `snd_replay` takes `$SND_SET`, a directory of the set's
region images (`m2hle --export-roms`), and the same stimuli or a capture of the
game's own commands. 60 s per input, timing median / p95 |err| in ms, % of the
board's key-ons matched within 30 ms:

| build | bgm | sfx | sys | fuzz |
|---|---|---|---|---|
| sfight | 100.0%, -0.9 / 4.5 | 99.7%, -5.0 / 8.5 | 99.8%, -0.9 / 3.0 | 100.0%, -2.9 / 5.4 |
| skytargt | 99.7%, -1.3 / 5.0 | 99.6%, -4.7 / 6.7 | 99.2%, -1.3 / 5.6 | 100.0%, -5.9 / 9.5 |
| von | 100.0%, -3.0 / 6.9 | 99.9%, -3.1 / 9.2 | 100.0%, -3.4 / 7.8 | 100.0%, -4.5 / 5.7 |
| gunblade | 100.0%, -2.5 / 6.6 | 100.0%, -9.3 / 14.9 | 99.7%, -2.5 / 6.8 | 100.0%, -5.0 / 8.5 |
| vcop2 | 100.0%, -0.9 / 2.7 | 99.6%, -10.7 / 17.6 | 100.0%, +0.3 / 3.8 | 100.0%, -3.9 / 4.9 |
| airwlkrs | 99.6%, -0.9 / 4.1 | 99.5%, -1.7 / 5.3 | 99.8%, -1.1 / 10.4 | 100.0%, -0.9 / 1.2 |
| bel | 100.0%, -2.1 / 9.3 | 97.5%, -1.7 / 15.3 | 100.0%, -2.3 / 7.8 | 100.0%, -0.8 / 0.9 |

Manx TT on its engine inputs (`eng`, `engo`, `eng2`): 100% each, -1.1 / 2.8,
-1.2 / 3.3, -1.1 / 3.2. Dead or Alive on 180 s of its own captured commands:
3311 key-ons, 99.9%, +1.65 / 6.5. Two known differences:

- bel's sfx: 822 key-ons in C against the board's 767. They are all its
  placeholder effect (sample 0x7800 at TL 119, near silence), which STF's
  effect numbers land on, and how many get through is queue order.
- airwlkrs's fuzz: 6% quieter for 8 s with the same notes and registers: two
  voices on the same sample keyed 1.4 ms apart instead of 0.9 (one event per
  main-loop pass), so they interfere differently.

**What made the timing.** The timers drift with the driver's load, and three
board facts set it (all `SND_TRACE` measurements):

- *A pass over the slots is longer when it copies.* Only a real refill (a whole
  chunk, a loop end, the last chunk) takes the 16 `movem.l` block; a stream
  waiting on the chip's half is a few instructions. Dead or Alive's board
  passes every ~61 samples, not 43, and the stream step's pacing is per pass,
  so the copy rate follows (`shle_refill_pass`, `pass_rem`).
- *Timer A hides B and C.* The SCSP raises level 1 for the first pending
  enabled source in the order A, B, C, MIDI, so a B behind a pending A enters
  225 cycles after A's handler does (`SHLE_ACK_A`: its ack is that far in).
- *The main loop masks timer A during a MIDI event* (`move.w #$2100,sr` round
  the channel-event dispatch, STF 0x601A78): an A that comes then waits for
  the event, and a B behind it with it -- the board's 2,000-6,700-cycle B
  latencies. `amask_from`/`amask_until`; A0-status commands are not masked.

## The voices in C (`--sound-synth`)

`src/board/sound_synth.h` takes the next step (Pinboard #1737): under the
driver in C, the 32 voices are played in C too, with no SCSP slots. The driver
keys a note and the synth plays the sample at its pitch, through its envelope,
LFO and pan, straight into the mix: `--sound-synth`, `M2HLE_SOUND_SYNTH=1`,
the libretro option "Sound driver" = "In C, voices too", `SND_SYNTH=1` in
`snd_replay`/`snd_bench`. It implies `--sound-hle` and, like it, takes only
the driver builds that one knows; anything else stays on the 68000 and the chip.

**What stays the chip's.** The reverb DSP, its MIXS inputs, the direct outs'
sum, MVOL and the DAC: common and DSP register writes still go to
`g_sound.scsp` (`ssyn_chip_write`), and the synth hands each block's voice
sums and MIXS to the chip's own mixer. The DSP programs are the game's, and
they are not per-note work.

**Exact, by construction.** Each voice is MAME's arithmetic, not an
approximation of it: the pitch step `(1024 + FNS) << (OCT + 2)` in 1/4096ths
of a sample, the linear interpolation between the two samples either side,
the envelope's rate table and its attack curve, the LFO's waves and its
pitch/amplitude scale tables, the TL/pan gain tables, the loop modes
(forward, reverse, alternating), and 8- and 16-bit PCM. So what it plays is
the chip's sample for sample, and `tools/grade-sound-synth.py` checks that by
identity: over `snd_replay`'s bgm, sfx, sys and fuzz inputs, with and without
`SCSP_EXACT`, the audio, the driver's register reads, its RAM and the
register file are the same bytes. Since the driver in C is graded against the
68000 (above) and the chip against MAME, the synth carries both gradings.

**What makes it exact, besides the maths:**

- *It catches up where the chip would.* The chip runs its slots up to "now"
  (`scsp_sync`) on a slot write, on a common write that `scsp_write_syncs`,
  on a sound RAM write inside the DSP's ring, at a 1024-sample block and at
  the end of the driver's run; the synth flushes at exactly those points, so
  the quiet-DSP bookkeeping (`scsp_dsp_due`, the frozen ring) skips the same
  samples.
- *A read of one voice runs only that voice.* The driver polls a voice's CA
  (the streaming refill) and writes sample RAM outside the DSP ring; the chip
  runs that voice alone up to now for those, and so does the synth
  (`done[32]`, `ssyn_ca_upper`, `ssyn_ram_touch`).
- *Peeks don't catch up* (the capture's register dumps, MCP's `read_comm`),
  as on the chip (`scsp_peek16`); they read the synth's registers when it is
  on (`ssyn_scsp_peek16`).

**The i960 cannot tell either**: the UART is the same, the driver in C is the
same, and only what the voices compute moved. `det_digest --sound --sound-synth`
against `--sound-hle`: same digest and same PCM over 3600 frames of attract;
with `--rewind 8`, 148 flat-state loads, still the same. No protocol bump.

**Cost.** A note is a sample pointer, a step and an envelope; a steady voice
(releasing or holding in decay 2, no LFO, a forward loop or none) takes a
tight loop into a buffer and a vectorised mix. The effects input (30 s) runs
in 0.20 s against 0.216 s through the slots (`snd_bench`), about 7% less;
the DSP is still most of what is left.

**Savestates.** The synth's state is the `SNDSYNTH` entry (and rollback's
`SSYN` part). It is optional: a state saved without it loads with the synth
off and the chip's slots in charge.
