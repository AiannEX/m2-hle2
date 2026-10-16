/*
 * profiles/vonj.h — Cyber Troopers Virtual-On, Twin (vonj: Japan, Revision B).
 *
 * MAME ROM set: vonj, a clone of von (MAME src/mame/sega/model2.cpp,
 * ROM_START(vonj)): only the first program pair is its own, the rest comes
 * from von.zip. ROM names and CRCs are MAME's; the chips are found by CRC as
 * well, so a set that labels them otherwise still loads.
 *
 * Board: Model 2B CRX, the board STF and Fighting Vipers run on, so the
 * profile is the board's defaults, but for the COP, which runs the program
 * the game uploads (model2b_cop.h): interrupts through the program's own table
 * (irq_vectors), the frame edge at the vblank (board_vblank), no hooks.
 *
 * The comm board (cpu3, epr-18643a.7) that links the two cabinets of a Twin
 * is there with nobody on the link (m2comm.h), as in MAME: a board on its own
 * is one cabinet, a player against the CPU. The ROM's link protocol is in YAMP's notes (newyamp, docs/von-*.md).
 *
 * Controls: two levers, each with a trigger (shot) and a thumb button (dash)
 * (MAME INPUT_PORTS_START(von)). IN1 is the left lever, IN2 the right. The
 * left lever is player 1's directions and B1 / B2, the right lever player 2's
 * directions and B1 / B2 (its port is STF's player 2 port). Both levers the same
 * way walk, opposite ways turn, both out jump, both in crouch. P1 B3 is
 * both triggers (the centre weapon) and P1 B4 both dashes.
 */
#ifndef PROFILES_VONJ_H
#define PROFILES_VONJ_H

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
#include "model2b_cop.h"
#include "m2comm.h"
#include "savestate_reg.h"

static const rom_part_t vonj_parts[] = {
    ROM_PAIR32(ROM_MAINCPU,    0x0000000, "epr-18664b.15", 0x27d0172c, "epr-18665b.16", 0x2f0142ee),
    ROM_PAIR32(ROM_MAINCPU,    0x0100000, "epr-18666.13",  0x66edb432, "epr-18667.14",  0xb593d31f),
    ROM_PAIR32(ROM_MAIN_DATA,  0x0000000, "mpr-18648.11",  0x2edbe489, "mpr-18649.12",  0xe68c5aa6),
    ROM_PAIR32(ROM_MAIN_DATA,  0x0800000, "mpr-18650.9",   0x89a855b9, "mpr-18651.10",  0xf4c23107),
    ROM_PAIR32(ROM_COPRO_DATA, 0x0000000, "mpr-18662.29",  0xa33d3335, "mpr-18663.30",  0xea74a641),
    ROM_PAIR32(ROM_POLYGONS,   0x0000000, "mpr-18654.17",  0x6a0caf29, "mpr-18655.21",  0xa4293e78),
    ROM_PAIR32(ROM_POLYGONS,   0x0800000, "mpr-18656.18",  0xb4f51e76, "mpr-18657.22",  0xa9be4674),
    ROM_PAIR32(ROM_TEXTURES,   0x0000000, "mpr-18660.27",  0xe53663e5, "mpr-18658.25",  0x3d0fcd01),
    ROM_PAIR32(ROM_TEXTURES,   0x0800000, "mpr-18661.28",  0x52b50410, "mpr-18659.26",  0x27aa8ae2),
    ROM_SWAP16(ROM_AUDIOCPU,   0x0000000, "epr-18670.31",  0x3e715f76),
    ROM_SWAP16(ROM_SAMPLES,    0x0000000, "mpr-18652.32",  0x037eee53),
    ROM_SWAP16(ROM_SAMPLES,    0x0400000, "mpr-18653.34",  0x9ec3e7bf),
};

static const rom_recipe_t vonj_recipe = {
    .set  = "vonj",
    .size = { [ROM_MAINCPU] = 0x200000, [ROM_MAIN_DATA] = 0x2000000,
              [ROM_COPRO_DATA] = 0x800000,
              [ROM_POLYGONS] = 0x1000000, [ROM_TEXTURES] = 0x1000000,
              [ROM_AUDIOCPU] = 0x80000, [ROM_SAMPLES] = 0x800000 },
    .parts = vonj_parts, .count = sizeof vonj_parts / sizeof vonj_parts[0],
};

static inline int vonj_load(romset_t *rs, const char *child, const char *parent) {
    return rom_recipe_load(rs, child, parent, &vonj_recipe);
}

static inline uint32_t vonj_read32(const uint8_t *buf, uint32_t off) {
    return (uint32_t)buf[off] | (uint32_t)buf[off + 1] << 8
         | (uint32_t)buf[off + 2] << 16 | (uint32_t)buf[off + 3] << 24;
}

static inline void vonj_install(const romset_t *rs, i960_cpu_t *cpu, memory_bus_t *bus) {
    if (!rs->loaded) { LOG_ERROR("vonj_install: romset not loaded"); return; }

    i960_reset(cpu);
    mem_init(bus, rs->maincpu, rs->maincpu_size);

    /* The COP runs Virtual-On's own program, not STF's (model2b_cop.h), and
     * reads its sine/cosine tables straight out of copro_data. */
    model2b_cop_install(bus, rs->copro_data, rs->copro_data_size);
    savestate_extra("vonj", "M2BCOP", &g_m2bc, sizeof g_m2bc);
    savestate_extra("vonj", "SHARC", &g_shc, sizeof g_shc);
    /* A comm board with no other cabinet on the link: without one the
     * machine select waits for a link state forever (m2comm.h). */
    m2comm_install(bus, 0x1C0u);
    savestate_extra("vonj", "M2COMM", &g_m2comm, sizeof g_m2comm);

    /* main_data at 0x02000000; the "extra" window at 0x06000000 is main_data
     * from 0x1000000 (MAME model2_base_mem), which Virtual-On leaves empty. */
    if (rs->main_data && bus->main_data) {
        size_t n = rs->main_data_size < MAIN_DATA_SIZE ? rs->main_data_size : MAIN_DATA_SIZE;
        memcpy(bus->main_data, rs->main_data, n);
    }
    if (rs->main_data && bus->xtra_data && rs->main_data_size > 0x1000000u) {
        size_t n = rs->main_data_size - 0x1000000u;
        memcpy(bus->xtra_data, rs->main_data + 0x1000000u, n < XTRA_DATA_SIZE ? n : XTRA_DATA_SIZE);
    }

    /* The processor starts on the PRCB's interrupt stack, at priority 31,
     * interrupted, supervisor (MAME i960 device_reset), as sfight_install. */
    uint32_t prcb = vonj_read32(rs->maincpu, 0x04);
    cpu->prcb = prcb;
    if (prcb + 0x2C < rs->maincpu_size) {
        uint32_t isp = vonj_read32(rs->maincpu, prcb + PRCB_INTR_STACK);
        cpu->globals.fp = isp;
        cpu->locals.sp  = isp + 64;
        cpu->sfr.pc     = 0x001F2002u;
        uint32_t start_ip_ptr = vonj_read32(rs->maincpu, prcb + PRCB_START_IP);
        if (start_ip_ptr < rs->maincpu_size) cpu->sfr.ip = vonj_read32(rs->maincpu, start_ip_ptr);
    }
    LOG_INFO("vonj_install: PRCB=0x%08X initial IP=0x%08X", prcb, cpu->sfr.ip);
}

/* IN0 is the cabinet's: COIN1 0x01, COIN2 0x02, SERVICE 0x04, START 0x10
 * (MAME INPUT_PORTS_START(model2crx)). IN1 (byte 1) the left lever, IN2
 * (byte 2) the right: shot 0x01, dash 0x02, down 0x10, up 0x20, right 0x40,
 * left 0x80 (INPUT_PORTS_START(von)). */
#define VONJ_L_SHOT 0x00000100u
#define VONJ_L_DASH 0x00000200u
#define VONJ_R_SHOT 0x00010000u
#define VONJ_R_DASH 0x00020000u

static const game_profile_t vonj_profile = {
    .id              = "vonj",
    .display_name    = "Cyber Troopers Virtual-On (Japan, Rev B)",
    .parent_zip_name = "von.zip",
    .board           = BOARD_MODEL2B_CRX,
    .load_fn         = vonj_load,
    .install_fn      = vonj_install,
    .hook_count      = 0,
    .input = {
        .bits = {
            [GAME_INPUT_P1_COIN]  = 0x00000001,
            [GAME_INPUT_P2_COIN]  = 0x00000002,
            [GAME_INPUT_SERVICE]  = 0x00000004,
            [GAME_INPUT_P1_START] = 0x00000010,
            [GAME_INPUT_P1_DOWN]  = 0x00001000,
            [GAME_INPUT_P1_UP]    = 0x00002000,
            [GAME_INPUT_P1_RIGHT] = 0x00004000,
            [GAME_INPUT_P1_LEFT]  = 0x00008000,
            [GAME_INPUT_P1_B1]    = VONJ_L_SHOT,
            [GAME_INPUT_P1_B2]    = VONJ_L_DASH,
            [GAME_INPUT_P1_B3]    = VONJ_L_SHOT | VONJ_R_SHOT,
            [GAME_INPUT_P1_B4]    = VONJ_L_DASH | VONJ_R_DASH,
            [GAME_INPUT_P2_DOWN]  = 0x00100000,
            [GAME_INPUT_P2_UP]    = 0x00200000,
            [GAME_INPUT_P2_RIGHT] = 0x00400000,
            [GAME_INPUT_P2_LEFT]  = 0x00800000,
            [GAME_INPUT_P2_B1]    = VONJ_R_SHOT,
            [GAME_INPUT_P2_B2]    = VONJ_R_DASH,
        },
    },
    .quirks = {
        .enable_68k_sound = true,
        .irq_vectors      = true,
        .board_vblank     = true,
        /* vonj is the Japanese program: its test menu has no country, and
         * MAME's von and vonu (the other countries) are other program ROMs, not a setting. */
        .region_fixed     = "the program ROM is Japan's (von, vonu are other sets)",
    },
};

#endif /* PROFILES_VONJ_H */
