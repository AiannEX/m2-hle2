# Virtual-On: the PS3 port's menus, and the way to online play

Pinboard #957 (Virtual-On, part 6). The PS3 release of Cyber Troopers Virtual-On
(NPJB00321, PSN, 2012) was read in Ghidra. It is Sega's m2ftg emulator wrapper,
the same family as PS3 Sonic the Fighters (TaskSyncIo lockstep, cellRudp,
NPMatching2Session, comId NPWR04277), around the **vonj** program: the words
its hook table patches match vonj's (0x18AB4 `ld 0x5039F4`, 0x1871C
`st -> 0x5024D4`).

What came of it: the libretro core's PS3 shell runs for `vonj` with the PS3's
Arcade settings (`ps3ui_rules_von`, `vonj_apply_menu_settings`). Versus and
Online Battle are refused, for the reason below.

## Landmarks in the EBOOT (addresses as Ghidra loads it)

| What | Where |
|---|---|
| i960 symbol table (276 names, ROM and RAM) | 0x00441070 |
| HLE hook table `{u32 i960_pc, u32 opd}`, 127 entries, ends 0,0 | 0x00472CE8..0x004730E0 |
| Board API vtable: get_board 0x184AD0, select_board 0x184D68, step_frame 0x184DF8, reset 0x184EB8, init(board_count) 0x184FD0 | 0x004550E0 |
| Frame driver: board 0, then board 1 in two-board mode; half rate when MainMode 4 / SubMode 0x0A | FUN_00184DF8 |
| Board link: a local 0x700 memcpy between the two boards' comm RAM | FUN_00185988 |
| Write a byte into the board's memory | FUN_0018F6AC(addr, &byte) |
| Main menu: Init (item states +0x3F8..+0x402), Decide | FUN_0011DFDC, FUN_0011E5D8 |
| Arcade menu vtable / Init / Update | 0x00446A14 / 0x11BF24 / 0x11B880 |
| Versus menu vtable / Init / Update | 0x004469DC / 0x11DDC8 / 0x11B5F0 |
| Training, Main, Option, Pause, MultiRule vtables | 0x446934, 0x446A4C, 0x4423DC, 0x44732C, 0x446FE4 |
| GameMode_Set(mode, rec[7]): mode 0 Arcade, 1 Versus, 2 (online) | FUN_000CBEC4 |
| Arcade apply / Versus apply (= Arcade + 0x1D00024 = 1) | FUN_0019F0E4 / FUN_0019F28C |
| Difficulty map {3,3,0,1,2}; play-time map +8 {5,11,17} | table at 0x3D3FB0 (TOC slot 0x461DDC) |

Menu text is message ids into the PS3's data files (not in the EBOOT); the rows
were named from what the apply functions write and from the cabinet's own test
menu strings.

**Arcade menu**: five rows, maximums {4,4,3,1,1} (table 0x3D2BA8 + 0x28), the
saved values at `FUN_000E75B8() + 8`. Decide builds `{row0, !trial, 0, row2,
row1, row3, row4}` and calls GameMode_Set(0). The apply writes:

| Backup RAM | Cabinet test menu (GAME ASSIGNMENTS) | From |
|---|---|---|
| 0x1D00016, 18, 1A | PLAY TIME 1P stage 1~5 / stage 6~8 / versus: (n + 1) * 5 s, 0xFF DEATH-MATCH | rec[3] (0..2 -> 30/60/90 s, 3 -> 0xFF) |
| 0x1D0001B, 1C, 1D, 1F | MATCH COUNT: rounds to win - 1 | rec[4] |
| 0x1D00021 | GAME DIFFICULTY: 0 NORMAL, 1 HARD, 2 VERY HARD, 3 EASY | rec[0] through the map |
| 0x1D00024 | two-board versus (Versus only) | 1 |

0x1D00017 and 0x1D00019 (the penalty and last-stage times) are left alone. Rows
3 and 4 are not written to the board. Factory values (a cleared board):
16..1A = 11 0B 11 11 11 (90, 60, 90, 90, 90 s), 1B..1F = 0, 21 = 0, 34 (free
play) = 0, 28 (cabinet role) = 2 NOLINK.

The game reads these when a game starts, so writing them during the attract
is enough; no reset is needed. This was checked headless: 0x1D00016 = 5 gave a
30 s round, 0x1D0001B = 1 a ROUND 2, 0x1D00034 = 1 FREEPLAY at once.

**Versus menu** (Update 0x11B5F0): two rows, a record with match count and
play time, then `init(1)` on the board API and GameMode_Set(1).

## Why Versus and Online are refused here

A Virtual-On board is one cabinet: one player and the CPU. A versus is two
cabinets linked by their comm boards (a "Twin"). The PS3 runs **both boards in
one process**:
- Its frame driver steps board 0 and then board 1.
- The link is a memcpy of the 0x700-byte comm window between them.
- Online, it sends the pads (lockstep) and each PS3 runs both boards.

m2-hle2 runs one board: its state is in globals (g_m2bc, g_shc, g_m2comm,
g_sound, ...). So Offline Versus needs a two-board core first.

## RPCN: the plan (from YAMP's notes)

YAMP's Virtual-On netplay (rokuYAMP `docs/von-handoff.md`, `von-comm-packet.md`,
`von-netplay-recon.md`) settled on a different design from the PS3's, after
lockstep failed four times. It works over RPCN on two machines. It fits
m2-hle2's one board:

- **One board per machine.** The ROM's own link protocol goes over the wire:
  each frame, the 0x700-byte comm window (cSend 0x5032F0 -> comm RAM 0x1A12000;
  peer's -> 0x1A12700 -> cRecn 0x5024F0).
  - It needs no lockstep and no determinism.
  - Packets may be lost, duplicated or reordered: the ROM's check is
    self-contained (`[+0x556] == [+2] ^ 0xAE5E`).
  - YAMP sends each one RLE-packed (~120 B) three times.
- **The room gives the role**: host = MASTER, guest = SLAVE, through the
  cabinet-role byte 0x1D00028 (2 NOLINK, 1 MASTER, 0 SLAVE) and a soft reset
  into it. Transmit only once the peer's ROM has released its comm board
  (CommBoardReset 0x01A14000 bit 0).
- **Both boards at 60 Hz, and one packet taken per board frame.** The stage
  handshake (state 0x24 at payload +4, the stage at +8) lives for a single
  frame. A board that reads less often than the peer writes misses it, and the
  two load different stages.
- The comm board itself is MAME's m2comm.cpp with a peer: ring up, node id from
  the role, node count 2. m2comm.h today is the "nobody on the link" half.

What that means here:
- The link is not a `netplay.h` lockstep session. It is a raw datagram channel
  in a room.
- What the i960 reads becomes the peer's packets, which is outside det_digest
  and rollback: a Virtual-On link session has no savestate rewind and no
  desync check, by design.
- It needs its own `NETPLAY_PROTO_REV`.

Steps:
1. Port MAME's linked m2comm (ring, node id, node count) behind a peer
   interface in m2comm.h.
2. Add a packet channel to the RPCN P2P socket (`net/`) carrying the window.
3. Set the role from the room.
4. Pace the board to 60 Hz while linked.
5. Test two headless clients on the container's RPCN.

The shell's Online Battle row then opens the lobby for vonj (`no_online` =
NULL).

## GGPO: not for Virtual-On

GGPO rolls back one deterministic emulation that holds both players. For STF
that is one board. For Virtual-On it would be both boards on every peer: the
two-board core from above, at twice the CPU, re-simulated on every rollback.
That is too heavy for the handhelds the core runs on. The RPCN link needs
neither the second board nor determinism. So the choice is the link, and no
GGPO for Virtual-On.
