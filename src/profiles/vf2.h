/*
 * profiles/vf2.h — Virtua Fighter 2 (Pinboard #1046).
 *
 * A Model 2A game (MAME src/mame/sega/model2.cpp, model2a_state, ROM_START(vf2)
 * and its clones), on the board Sega Rally runs on: the TGP running the
 * program the i960 uploads at boot (board/tgp.h, board/model2a.h), the SCSP
 * sound board, digital controls. The four sets share every chip but the
 * program's four:
 *
 *   vf2    Version 2.1   epr-18385..18388
 *   vf2b   Revision B    epr-17568b, 17569b, 17562b, 17563b
 *   vf2a   Revision A    epr-17568a, 17569a, 17562a, 17563a
 *   vf2o   the original  epr-17568, 17569, 17562, 17563
 *
 * What is known of its data is noclip's (vendor/noclip js/games.js `vf2`,
 * js/vf2.js, js/vf2fighters.js), read off the 2.1 program: the model table at
 * data 0xE0004 (`lda 0x20E0004[g0*16]` in the draw at 0x7CB8), 5125 entries
 * of 16 bytes up to the run of 0xFF at 0xF4054; the stage byte at 0x500064,
 * the roster's order. The clones' programs are other builds, so an address in
 * the program ROM is 2.1's only.
 *
 * Controls: MAME INPUT_PORTS_START(vf2), which Fighting Vipers shares: IN0 the
 * cabinet's (COIN1 0x01, COIN2 0x02, TEST 0x04, SERVICE 0x08, START1 0x10,
 * START2 0x20), IN1 player 1 and IN2 player 2 (Punch 0x01, Kick 0x02, Guard
 * 0x04, down 0x10, up 0x20, right 0x40, left 0x80).
 */
#ifndef PROFILES_VF2_H
#define PROFILES_VF2_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "constants.h"
#include "log.h"
#include "rom_loader.h"
#include "rom_recipe.h"
#include "game_profile.h"
#include "memory.h"
#include "i960.h"
#include "model2a.h"
#include "savestate_reg.h"

/* Everything but the program: data, polygons, textures, sound, and the TGP's
 * tables on the 2A's CPU board (MODEL2_CPU_BOARD). */
#define VF2_COMMON \
    ROM_PAIR32(ROM_MAIN_DATA,  0x000000, "mpr-17560.10",  0xd1389864, "mpr-17561.11",  0xb98d0101), \
    ROM_PAIR32(ROM_MAIN_DATA,  0x400000, "mpr-17558.8",   0x4b15f5a6, "mpr-17559.9",   0xd3264de6), \
    ROM_PAIR32(ROM_MAIN_DATA,  0x800000, "mpr-17566.6",   0xfb41ef98, "mpr-17567.7",   0xc3396922), \
    ROM_PAIR32(ROM_MAIN_DATA,  0xc00000, "mpr-17564.4",   0xd8062489, "mpr-17565.5",   0x0517c6e9), \
    ROM_PAIR32(ROM_POLYGONS,   0x000000, "mpr-17554.16",  0x27896d82, "mpr-17548.20",  0xc95facc2), \
    ROM_PAIR32(ROM_POLYGONS,   0x400000, "mpr-17555.17",  0x4df2810b, "mpr-17549.21",  0xe0bce0e6), \
    ROM_PAIR32(ROM_POLYGONS,   0x800000, "mpr-17556.18",  0x41a47616, "mpr-17550.22",  0xc36ff3f5), \
    ROM_PAIR32(ROM_TEXTURES,   0x000000, "mpr-17553.25",  0x5da1c5d3, "mpr-17552.24",  0xe91e7427), \
    ROM_PAIR32(ROM_TEXTURES,   0x800000, "mpr-17547.27",  0xbe940431, "mpr-17546.26",  0x042a194b), \
    ROM_SWAP16(ROM_AUDIOCPU,   0x000000, "epr-17574.30",  0x4d4c3a55), \
    ROM_SWAP16(ROM_SAMPLES,    0x000000, "mpr-17573.31",  0xe43557fe), \
    ROM_SWAP16(ROM_SAMPLES,    0x200000, "mpr-17572.32",  0x4febecc8), \
    ROM_SWAP16(ROM_SAMPLES,    0x400000, "mpr-17571.36",  0x51caa584), \
    ROM_SWAP16(ROM_SAMPLES,    0x600000, "mpr-17570.37",  0xbccd324b), \
    ROM_PAIR32(ROM_TGP_TABLES, 0x000000, "opr-14742a.45", 0x90c6b117, "opr-14743a.46", 0xae7f446b)

#define VF2_PROGRAM(a, a_crc, b, b_crc, c, c_crc, d, d_crc) \
    ROM_PAIR32(ROM_MAINCPU, 0x000000, a, a_crc, b, b_crc), \
    ROM_PAIR32(ROM_MAINCPU, 0x040000, c, c_crc, d, d_crc)

static const rom_part_t vf2_parts[] = {
    VF2_PROGRAM("epr-18385.12", 0x78ed2d41, "epr-18386.13", 0x3418f428,
                "epr-18387.14", 0x124a8453, "epr-18388.15", 0x8d347980), VF2_COMMON,
};
static const rom_part_t vf2b_parts[] = {
    VF2_PROGRAM("epr-17568b.12", 0x5d966bbf, "epr-17569b.13", 0x0b8c1ccc,
                "epr-17562b.14", 0xb778d4eb, "epr-17563b.15", 0xa05c15f6), VF2_COMMON,
};
static const rom_part_t vf2a_parts[] = {
    VF2_PROGRAM("epr-17568a.12", 0x5b10f232, "epr-17569a.13", 0x17c208e0,
                "epr-17562a.14", 0xdb68a01a, "epr-17563a.15", 0x4696439d), VF2_COMMON,
};
static const rom_part_t vf2o_parts[] = {
    VF2_PROGRAM("epr-17568.12", 0xcf5d53d1, "epr-17569.13", 0x0fb32808,
                "epr-17562.14", 0xb893bcef, "epr-17563.15", 0x3b55f5a8), VF2_COMMON,
};

/* copro_data is empty (ROMREGION_ERASE00): VF2 has no course data for the TGP. */
#define VF2_RECIPE(name) \
    static const rom_recipe_t name##_recipe = { \
        .set = #name, \
        .size = { [ROM_MAINCPU] = 0x200000, [ROM_MAIN_DATA] = 0x2400000, \
                  [ROM_COPRO_DATA] = 0x800000, [ROM_POLYGONS] = 0x2000000, \
                  [ROM_TEXTURES] = 0x1000000, [ROM_AUDIOCPU] = 0x80000, \
                  [ROM_SAMPLES] = 0x800000, [ROM_TGP_TABLES] = 0x40000 }, \
        .parts = name##_parts, .count = sizeof name##_parts / sizeof name##_parts[0], \
    }; \
    static inline int name##_load(romset_t *rs, const char *child, const char *parent) { \
        return rom_recipe_load(rs, child, parent, &name##_recipe); \
    }
VF2_RECIPE(vf2)
VF2_RECIPE(vf2b)
VF2_RECIPE(vf2a)
VF2_RECIPE(vf2o)
#undef VF2_RECIPE
#undef VF2_PROGRAM
#undef VF2_COMMON

static inline uint32_t vf2_read32(const uint8_t *buf, uint32_t off) {
    return (uint32_t)buf[off] | (uint32_t)buf[off + 1] << 8
         | (uint32_t)buf[off + 2] << 16 | (uint32_t)buf[off + 3] << 24;
}

static inline void vf2_install(const romset_t *rs, i960_cpu_t *cpu, memory_bus_t *bus) {
    if (!rs->loaded) { LOG_ERROR("vf2_install: romset not loaded"); return; }

    i960_reset(cpu);
    mem_init(bus, rs->maincpu, rs->maincpu_size);
    model2a_install(bus, rs->tgp_tables, rs->copro_data, rs->copro_data_size);

    /* main_data at 0x02000000; the "extra" window at 0x06000000 is main_data
     * from 0x1000000 (MAME model2_base_mem), which VF2 leaves empty. */
    if (rs->main_data && bus->main_data) {
        size_t n = rs->main_data_size < MAIN_DATA_SIZE ? rs->main_data_size : MAIN_DATA_SIZE;
        memcpy(bus->main_data, rs->main_data, n);
    }
    if (rs->main_data && bus->xtra_data && rs->main_data_size > 0x1000000u) {
        size_t n = rs->main_data_size - 0x1000000u;
        memcpy(bus->xtra_data, rs->main_data + 0x1000000u, n < XTRA_DATA_SIZE ? n : XTRA_DATA_SIZE);
    }

    uint32_t prcb = vf2_read32(rs->maincpu, 0x04);
    cpu->prcb = prcb;
    if (prcb + 0x2C < rs->maincpu_size) {
        uint32_t start_ip_ptr = vf2_read32(rs->maincpu, prcb + PRCB_START_IP);
        if (start_ip_ptr < rs->maincpu_size) cpu->sfr.ip = vf2_read32(rs->maincpu, start_ip_ptr);
    }
    /* The TGP is board state outside the parts savestate.h lists (srallyc.h). */
    savestate_extra("vf2", "TGP", &g_tgp, sizeof g_tgp);
    LOG_INFO("vf2_install: PRCB=0x%08X initial IP=0x%08X", prcb, cpu->sfr.ip);
}

#define VF2_INPUT { .bits = { \
    [GAME_INPUT_P1_COIN]  = 0x00000001, [GAME_INPUT_P2_COIN]  = 0x00000002, \
    [GAME_INPUT_TEST]     = 0x00000004, [GAME_INPUT_SERVICE]  = 0x00000008, \
    [GAME_INPUT_P1_START] = 0x00000010, [GAME_INPUT_P2_START] = 0x00000020, \
    [GAME_INPUT_P1_B1]    = 0x00000100, [GAME_INPUT_P1_B2]    = 0x00000200, \
    [GAME_INPUT_P1_B3]    = 0x00000400, \
    [GAME_INPUT_P1_DOWN]  = 0x00001000, [GAME_INPUT_P1_UP]    = 0x00002000, \
    [GAME_INPUT_P1_RIGHT] = 0x00004000, [GAME_INPUT_P1_LEFT]  = 0x00008000, \
    [GAME_INPUT_P2_B1]    = 0x00010000, [GAME_INPUT_P2_B2]    = 0x00020000, \
    [GAME_INPUT_P2_B3]    = 0x00040000, \
    [GAME_INPUT_P2_DOWN]  = 0x00100000, [GAME_INPUT_P2_UP]    = 0x00200000, \
    [GAME_INPUT_P2_RIGHT] = 0x00400000, [GAME_INPUT_P2_LEFT]  = 0x00800000, \
} }

/* Interrupts through the program's own table, the frame edge at the vblank,
 * no hooks: as Sega Rally and Dynamite Cop. The model table is noclip's
 * (games.js `vf2`): the mesh word is the polygon ROM's word address on the
 * bus, as in Sonic The Fighters and Fighting Vipers. */
#define VF2_QUIRKS { \
    .enable_68k_sound   = true, \
    .irq_vectors        = true, \
    .board_vblank       = true, \
    .mesh_ptr_subtract  = 0x02000010, \
    .mesh_ptr_add       = 0x10, \
    .model_table_offset = 0x000E0004, \
    .model_table_count  = 5125, \
    .region             = { .kind = REGION_TODO, \
                            .why  = "country setting not looked for yet" }, \
}

static const game_profile_t vf2_profile = {
    .id = "vf2", .display_name = "Virtua Fighter 2 (Version 2.1)",
    .board = BOARD_MODEL2A_CRX, .load_fn = vf2_load, .install_fn = vf2_install,
    .input = VF2_INPUT, .quirks = VF2_QUIRKS,
};
static const game_profile_t vf2b_profile = {
    .id = "vf2b", .display_name = "Virtua Fighter 2 (Revision B)", .parent_zip_name = "vf2.zip",
    .board = BOARD_MODEL2A_CRX, .load_fn = vf2b_load, .install_fn = vf2_install,
    .input = VF2_INPUT, .quirks = VF2_QUIRKS,
};
static const game_profile_t vf2a_profile = {
    .id = "vf2a", .display_name = "Virtua Fighter 2 (Revision A)", .parent_zip_name = "vf2.zip",
    .board = BOARD_MODEL2A_CRX, .load_fn = vf2a_load, .install_fn = vf2_install,
    .input = VF2_INPUT, .quirks = VF2_QUIRKS,
};
static const game_profile_t vf2o_profile = {
    .id = "vf2o", .display_name = "Virtua Fighter 2", .parent_zip_name = "vf2.zip",
    .board = BOARD_MODEL2A_CRX, .load_fn = vf2o_load, .install_fn = vf2_install,
    .input = VF2_INPUT, .quirks = VF2_QUIRKS,
};

#endif /* PROFILES_VF2_H */
