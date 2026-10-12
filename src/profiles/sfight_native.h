/*
 * sfight_native.h -- a round of Sonic the Fighters in the game's own replay
 * formats (Pinboard #602).
 *
 * STF keeps three kinds of replay, and a .m2replay (core/replay.h) carries all
 * three for every round it records, byte for byte as the game lays them out,
 * so they can be read with the game's own code or written back into it:
 *
 *  - The key bank (key_bank.bin, 0x1000 bytes at 0x531000, replay_bank_adr
 *    0x500168). It is what attract's Sonic-vs-Bean fight plays: ADV_REPLAY
 *    runs whatever is there. ROUND_DSP writes its header at 0xD008-0xD084 (+4
 *    stage_x, +0xC / +0x10 the 1P / 2P character, +0xFFF / +0xFFE each rob's
 *    +0x6CC) and key_rec_disp (0x37CE0) records two bytes a frame from +0x14,
 *    each rob's +0x1200 pad byte, bits 6 and 7 swapped for a rob facing left.
 *    It stops at 2048 frames by storing 0 into its task word. ROUND_DSP only
 *    starts it when STAGE_ID (0x500054) < 9 and not_scr_bg_move bit 4 is
 *    clear (0xCFF0), so a sudden-death round is not keyed.
 *  - The round-start snapshot it plays from: key_start.bin (0x572000, the two
 *    fighters as save_key_replay 0x37E94 keeps them) and effect_start.bin
 *    (0xC80 bytes at 0x573000). load_key_replay (called from key_play_init
 *    0x37D80) puts them back.
 *  - The ending slot (ending.bin, 0x8060 bytes): what the credits replay for
 *    each stage played. save_ending_rep_data (0x38464) writes it to 0x5A0010
 *    + (STAGE_ID - 1) * 0x8060 at the end of a round the game does not take to
 *    sudden death, and never in a versus game; load_ending_rep_data (0x385B4)
 *    reads it back. +0 stage_x, +4 dword_500434, +8 the s16 at mod_fa_obj0+6
 *    less 0xEF, +0xA 1 when byte_50004D is not 0, +0xC / +0xE the 1P / 2P
 *    character, +0xD / +0xF their rounds won, +0x10 the mod_fa_record task's
 *    words 0x40..0x83, +0x60 the 0x8000 bytes of rep_ram (0x535000), the
 *    instant replay's ring.
 *
 * All are taken at 0xDD20 in ROUND_DSP's judge (`ldob byte_50004D`): the round
 * is over, CTRL_TIMER has run out, and neither the sudden-death branch nor
 * not_sudden has run yet, so nothing the slot reads has moved. The ending slot
 * is built here the way save_ending_rep_data would build it, so a versus round
 * has one too, though the game itself never keeps it.
 *
 * round.bin is ours, not the game's: 16 bytes saying which round it was, for
 * the way back in (sfight_native_put).
 *   +0 "STFR"  +4 version (1)  +5 stage_num  +6 STAGE_ID  +7 flags
 *   +8 key frames (u16 LE)  +10 curr_round_num  +11 gameprogram  +12 byte_50004D
 *   flags: bit 0 the key bank was recorded this round, bit 1 it ran to its
 *   2048-frame end, bit 2 the game takes the round to sudden death.
 *
 * The hook only reads, and keeps what it read outside the board: a round that
 * nothing takes is dropped by the next replay that begins.
 */
#ifndef M2HLE_SFIGHT_NATIVE_H
#define M2HLE_SFIGHT_NATIVE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "log.h"
#include "game_profile.h"
#include "memory.h"
#include "i960.h"
#include "hle_hooks.h"

#define SFN_BANK          0x00531000u
#define SFN_BANK_LEN      0x1000u
#define SFN_BANK_KEYS     0x14u
#define SFN_KEY_START     0x00572000u
#define SFN_KEY_START_LEN 0x160u
#define SFN_EFFECT        0x00573000u
#define SFN_EFFECT_LEN    0xC80u
#define SFN_ENDING        0x005A0010u
#define SFN_ENDING_LEN    0x8060u
#define SFN_ENDING_SLOTS  10
#define SFN_REP_RAM       0x00535000u
#define SFN_REP_RAM_LEN   0x8000u
#define SFN_ROUND_LEN     16u

#define SFN_KEYED         1u
#define SFN_KEYS_FULL     2u
#define SFN_SUDDEN        4u

static struct {
    bool    pending;
    uint8_t bank[SFN_BANK_LEN];
    uint8_t key_start[SFN_KEY_START_LEN];
    uint8_t effect[SFN_EFFECT_LEN];
    uint8_t ending[SFN_ENDING_LEN];
    uint8_t round[SFN_ROUND_LEN];
} s_sfn;

static inline void sfn_read(memory_bus_t *bus, uint32_t addr, uint8_t *out, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) out[i] = mem_read8(bus, addr + i);
}

static inline void sfn_write(memory_bus_t *bus, uint32_t addr, const uint8_t *in, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) mem_write8(bus, addr + i, in[i]);
}

static inline void sfn_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* The slot as save_ending_rep_data would write it. */
static inline void sfn_build_ending(memory_bus_t *bus, uint8_t *e) {
    uint32_t rob0 = mem_read32(bus, 0x00500804), rob1 = mem_read32(bus, 0x00500808);
    memset(e, 0, SFN_ENDING_LEN);
    sfn_put32(e + 0, mem_read32(bus, 0x0050A00C));                 /* stage_x */
    sfn_put32(e + 4, mem_read32(bus, 0x00500434));
    uint32_t obj0 = mem_read32(bus, 0x00500870);                    /* mod_fa_obj0 */
    uint16_t y = (uint16_t)((int16_t)mem_read16(bus, obj0 + 6) - 0xEF);
    e[8] = (uint8_t)y; e[9] = (uint8_t)(y >> 8);
    e[0xA] = mem_read8(bus, 0x0050004D) ? 1 : 0;
    e[0xC] = mem_read8(bus, rob0 + 0x1B0);
    e[0xD] = mem_read8(bus, 0x0050004F);
    e[0xE] = mem_read8(bus, rob1 + 0x1B0);
    e[0xF] = mem_read8(bus, 0x00500051);
    sfn_read(bus, mem_read32(bus, 0x00500854) + 0x40, e + 0x10, 0x44);   /* mod_fa_record */
    sfn_read(bus, SFN_REP_RAM, e + 0x60, SFN_REP_RAM_LEN);
}

/* Did key_record run this round, and for how many frames? Read off its task
 * (mod_fa_key_record): bit 31 of word 0 is up while it records, and it zeroes
 * the word when the bank fills. A round after one that filled the bank and
 * that was not keyed itself reads as full; the header is then the old one. */
static inline uint32_t sfn_key_flags(memory_bus_t *bus, uint32_t *frames) {
    uint32_t task = mem_read32(bus, 0x0050084C);
    uint32_t word = mem_read32(bus, task);
    uint32_t off  = mem_read32(bus, task + 0x54) - (SFN_BANK + SFN_BANK_KEYS);
    *frames = off < SFN_BANK_LEN ? off / 2 : (SFN_BANK_LEN - SFN_BANK_KEYS) / 2;
    if (word & 0x80000000u) return SFN_KEYED;
    if (word == 0 && off >= 0x1000u && off < 0x2000u && mem_read8(bus, 0x00500054) < 9)
        return SFN_KEYED | SFN_KEYS_FULL;
    *frames = 0;
    return 0;
}

/* Does the judge send this round to sudden death? 0xDD28-0xDD50. */
static inline bool sfn_sudden(memory_bus_t *bus) {
    uint8_t p1 = mem_read8(bus, 0x0050004F), p2 = mem_read8(bus, 0x00500051);
    return mem_read8(bus, 0x0050004D) == 2 && p1 == p2 && mem_read8(bus, 0x00500052) <= p1;
}

/* round_end (0xDD20): observe only. */
static int sfight_hook_native_round(i960_cpu_t *cpu, memory_bus_t *bus) {
    (void)cpu;
    uint32_t frames = 0;
    uint32_t flags = sfn_key_flags(bus, &frames) | (sfn_sudden(bus) ? SFN_SUDDEN : 0);
    sfn_read(bus, SFN_BANK, s_sfn.bank, SFN_BANK_LEN);
    sfn_read(bus, SFN_KEY_START, s_sfn.key_start, SFN_KEY_START_LEN);
    sfn_read(bus, SFN_EFFECT, s_sfn.effect, SFN_EFFECT_LEN);
    sfn_build_ending(bus, s_sfn.ending);
    uint8_t *r = s_sfn.round;
    memset(r, 0, SFN_ROUND_LEN);
    memcpy(r, "STFR", 4);
    r[4]  = 1;
    r[5]  = mem_read8(bus, 0x00500064);   /* stage_num */
    r[6]  = mem_read8(bus, 0x00500054);   /* STAGE_ID */
    r[7]  = (uint8_t)flags;
    r[8]  = (uint8_t)frames;
    r[9]  = (uint8_t)(frames >> 8);
    r[10] = mem_read8(bus, 0x00500055);   /* curr_round_num */
    r[11] = mem_read8(bus, 0x0050004C);   /* gameprogram */
    r[12] = mem_read8(bus, 0x0050004D);
    s_sfn.pending = true;
    LOG_INFO("native replay: round %u on stage %u (STAGE_ID %u) ended, %u key frames%s%s",
             r[10], r[5], r[6], frames, (flags & SFN_KEYED) ? "" : " (not keyed)",
             (flags & SFN_SUDDEN) ? ", to sudden death" : "");
    return 1;
}

static inline uint32_t sfn_ending_addr(int stage_id) {
    return SFN_ENDING + (uint32_t)(stage_id - 1) * SFN_ENDING_LEN;
}

static bool sfight_native_take(game_native_round_t *out) {
    if (!s_sfn.pending) return false;
    s_sfn.pending = false;
    const uint8_t *r = s_sfn.round;
    int stage_id = r[6];
    uint32_t ending = stage_id >= 1 && stage_id <= SFN_ENDING_SLOTS ? sfn_ending_addr(stage_id) : 0;
    const game_native_part_t parts[] = {
        { "round.bin",        0,             s_sfn.round,     SFN_ROUND_LEN     },
        { "key_bank.bin",     SFN_BANK,      s_sfn.bank,      SFN_BANK_LEN      },
        { "key_start.bin",    SFN_KEY_START, s_sfn.key_start, SFN_KEY_START_LEN },
        { "effect_start.bin", SFN_EFFECT,    s_sfn.effect,    SFN_EFFECT_LEN    },
        { "ending.bin",       ending,        s_sfn.ending,    SFN_ENDING_LEN    },
    };
    out->nparts = (int)(sizeof parts / sizeof parts[0]);
    memcpy(out->part, parts, sizeof parts);
    snprintf(out->json, sizeof out->json,
             "{\"game\":\"stf\",\"round\":%u,\"stage\":%u,\"stage_id\":%d,\"keyed\":%d,"
             "\"key_frames\":%u,\"sudden\":%d,\"p1\":%u,\"p2\":%u,\"p1_wins\":%u,\"p2_wins\":%u}",
             r[10], r[5], stage_id, (r[7] & SFN_KEYED) ? 1 : 0, r[8] | (r[9] << 8),
             (r[7] & SFN_SUDDEN) ? 1 : 0, s_sfn.ending[0xC], s_sfn.ending[0xE],
             s_sfn.ending[0xD], s_sfn.ending[0xF]);
    return true;
}

static inline const game_native_part_t *sfn_find(const game_native_round_t *in, const char *name,
                                                 uint32_t len) {
    for (int i = 0; i < in->nparts; i++)
        if (!strcmp(in->part[i].name, name) && in->part[i].len == len) return &in->part[i];
    return NULL;
}

/*
 * The way back in: the key bank and its round-start snapshot go where attract
 * plays them from, and the round's stage pins attract's replay stage
 * (sfight_hook_replay_stage), so the next attract replay is this round. The
 * ending slot goes to the stage the round was played as (STAGE_ID), where the
 * credits read it. A part that is missing is left as the board has it.
 */
static const char *sfight_native_put(memory_bus_t *bus, const game_native_round_t *in) {
    const game_native_part_t *r = sfn_find(in, "round.bin", SFN_ROUND_LEN);
    if (!r || memcmp(r->data, "STFR", 4) || r->data[4] != 1) return "the round has no round.bin (version 1)";
    const game_native_part_t *bank = sfn_find(in, "key_bank.bin", SFN_BANK_LEN);
    const game_native_part_t *ks   = sfn_find(in, "key_start.bin", SFN_KEY_START_LEN);
    const game_native_part_t *eff  = sfn_find(in, "effect_start.bin", SFN_EFFECT_LEN);
    const game_native_part_t *end  = sfn_find(in, "ending.bin", SFN_ENDING_LEN);
    if (bank) sfn_write(bus, SFN_BANK, bank->data, SFN_BANK_LEN);
    if (ks)   sfn_write(bus, SFN_KEY_START, ks->data, SFN_KEY_START_LEN);
    if (eff)  sfn_write(bus, SFN_EFFECT, eff->data, SFN_EFFECT_LEN);
    int stage_id = r->data[6];
    if (end && stage_id >= 1 && stage_id <= SFN_ENDING_SLOTS)
        sfn_write(bus, sfn_ending_addr(stage_id), end->data, SFN_ENDING_LEN);
    if (bank) g_replay_stage_pin = r->data[5];
    return NULL;
}

#endif /* M2HLE_SFIGHT_NATIVE_H */
