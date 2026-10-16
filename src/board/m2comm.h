/*
 * m2comm.h — the Model 2 communication board with nobody on the other end of
 * the link (Pinboard #935).
 *
 * memory.h's COMM stub answers a game that only asks whether the board is
 * there. Virtual-On talks to it: its probe (0xC5870) writes CN 1 and waits for
 * the board's Z80 to toggle FG bit 7, and its machine select waits for the link
 * state its vblank handler reads from the shared RAM (0x5770B0, 0 = alone).
 * Read as "no board" (CN bit 0 set), it says so and the link state stays 0xFF,
 * so the select never comes.
 *
 * This is MAME's m2comm.cpp (M2COMM_SIMULATION) with the sockets open and no
 * message ever arriving, which is what MAME runs for one cabinet:
 *   0x01A00000..0x01A03FFF  shared RAM, mirrored +0x10000
 *   0x01A04000  CN: reads 0xFE | cn; a write of 1 starts the board (shared RAM
 *               zeroed, [1] = 2 "slave", frame size 0x0E00, frame offset), 0
 *               stops it
 *   0x01A04002  FG: reads 0x7E | fg | (zfg ? 0 : 0x80)
 * Each vblank while the board runs, the Z80 toggles zfg and writes "no link
 * yet" ([0] = 0, [2] = [3] = 0xFF). That runs lazily here: an access first
 * catches up on the vblanks counted since the last one (irqt), so nothing
 * hangs off the vblank and the result is the same.
 *
 * Every byte of state is in g_m2comm, POD; a profile that installs it saves it
 * as a savestate extra.
 */
#ifndef M2COMM_H
#define M2COMM_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "memory.h"
#include "irq_timer.h"

#define M2COMM_RAM_BASE 0x01A00000u
#define M2COMM_RAM_SIZE 0x4000u

typedef struct {
    uint8_t  shared[M2COMM_RAM_SIZE];
    uint8_t  cn, fg, zfg, enabled;
    uint32_t frame_offset;          /* MAME m_frameoffset: 0x1C0 (0x180 Power Sled) */
    uint64_t vbl_seen;              /* the vblank count the state is up to */
} m2comm_state_t;

static m2comm_state_t g_m2comm;

/* One Z80 pass (MAME comm_tick): link not established, never a message. */
static inline void m2comm__ticks(uint64_t n) {
    if (n & 1u) g_m2comm.zfg ^= 1u;
    g_m2comm.shared[0] = 0x00;
    g_m2comm.shared[2] = 0xFF;
    g_m2comm.shared[3] = 0xFF;
}

static inline void m2comm__catch_up(void) {
    irqt_flush();
    uint64_t now = (uint64_t)g_irqt.vbl_count;
    /* now < vbl_seen: irqt was reset under a running board; start counting again. */
    if (g_m2comm.enabled && now > g_m2comm.vbl_seen) m2comm__ticks(now - g_m2comm.vbl_seen);
    g_m2comm.vbl_seen = now;
}

static inline void m2comm__cn_w(uint8_t v) {
    g_m2comm.cn = v & 1u;
    if (!g_m2comm.cn) {
        g_m2comm.enabled = 0;
        g_m2comm.zfg = 0;
        g_m2comm.fg = 0;
        return;
    }
    g_m2comm.enabled = 1;
    memset(g_m2comm.shared, 0, sizeof g_m2comm.shared);
    g_m2comm.shared[0x01] = 0x02;
    g_m2comm.shared[0x12] = 0x00;
    g_m2comm.shared[0x13] = 0x0E;
    g_m2comm.shared[0x14] = (uint8_t)g_m2comm.frame_offset;
    g_m2comm.shared[0x15] = (uint8_t)(g_m2comm.frame_offset >> 8);
    m2comm__ticks(1);
}

static inline uint8_t m2comm__status_r(uint32_t off) {
    switch (off & 3u) {
        case 0:  return (uint8_t)(0xFEu | g_m2comm.cn);
        case 2:  return (uint8_t)(0x7Eu | g_m2comm.fg | (g_m2comm.zfg ? 0u : 0x80u));
        default: return 0;
    }
}

static uint32_t m2comm_status_read_cb(mem_region_t *r, uint32_t addr, int size) {
    m2comm__catch_up();
    uint32_t off = addr - r->base, v = 0;
    for (int k = size - 1; k >= 0; k--) v = (v << 8) | m2comm__status_r(off + (uint32_t)k);
    return v;
}

static void m2comm_status_write_cb(mem_region_t *r, uint32_t addr, uint32_t val, int size) {
    m2comm__catch_up();
    uint32_t off = addr - r->base;
    for (int k = 0; k < size; k++) {
        uint8_t b = (uint8_t)(val >> (8 * k));
        if (((off + (uint32_t)k) & 3u) == 0)      m2comm__cn_w(b);
        else if (((off + (uint32_t)k) & 3u) == 2) g_m2comm.fg = b & 1u;
    }
}

static uint32_t m2comm_ram_read_cb(mem_region_t *r, uint32_t addr, int size) {
    m2comm__catch_up();
    uint32_t off = addr - r->base, v = 0;
    for (int k = size - 1; k >= 0; k--) v = (v << 8) | g_m2comm.shared[(off + (uint32_t)k) & (M2COMM_RAM_SIZE - 1u)];
    return v;
}

static void m2comm_ram_write_cb(mem_region_t *r, uint32_t addr, uint32_t val, int size) {
    m2comm__catch_up();
    uint32_t off = addr - r->base;
    for (int k = 0; k < size; k++)
        g_m2comm.shared[(off + (uint32_t)k) & (M2COMM_RAM_SIZE - 1u)] = (uint8_t)(val >> (8 * k));
}

/* Call after mem_init: the COMM status regions answer as the board, and the
 * shared RAM (which mem_init leaves unmapped) is added with its mirror. */
static inline void m2comm_install(memory_bus_t *bus, uint32_t frame_offset) {
    memset(&g_m2comm, 0, sizeof g_m2comm);
    g_m2comm.frame_offset = frame_offset;
    irqt_flush();
    g_m2comm.vbl_seen = (uint64_t)g_irqt.vbl_count;
    for (int i = 0; i < bus->region_count; i++) {
        mem_region_t *r = &bus->regions[i];
        if (strcmp(r->name, "COMM") == 0) {
            r->read_cb  = m2comm_status_read_cb;
            r->write_cb = m2comm_status_write_cb;
        }
    }
    for (uint32_t base = M2COMM_RAM_BASE; base <= M2COMM_RAM_BASE + 0x10000u; base += 0x10000u) {
        mem_region_t *r = mem_add_region(bus, "COMM_RAM", base, M2COMM_RAM_SIZE, NULL, 0);
        if (r) {
            r->read_cb  = m2comm_ram_read_cb;
            r->write_cb = m2comm_ram_write_cb;
            r->no_burst = 1;
        }
    }
}

#endif /* M2COMM_H */
