#ifndef I960_TIMING_H
#define I960_TIMING_H

/* The board's i960: what an instruction costs on a Model 2 beyond MAME's
 * per-opcode table (game_quirks_t.i960_board_timing; Pinboard #744, issue #305).
 *
 * The shape is the 80960KB's own (Intel, 80960KB Programmer's Reference Manual,
 * appendix C, and the Hardware Designer's Reference Manual's L-bus chapter):
 *   - a 512-byte direct-mapped instruction cache, 32 lines of 16 bytes. The
 *     fetch unit runs ahead of execution, so continuing into a line that hits
 *     costs a cycle only when the line left held nothing slow enough to hide
 *     it. A miss is a 16-byte burst on the L-bus; a branch into a line that
 *     misses waits for the burst up to the word it lands on, and the line
 *     after it waits behind that burst, where running on into a line overlaps
 *     part of its burst with what was already queued.
 *   - a taken branch breaks the pipeline; one not taken costs a cycle less
 *     than the table's compare-and-branch.
 *   - loads are scoreboarded: the instruction after a load waits for the data
 *     only if it reads a loaded register (Table C-1; ldl/ldt/ldq scoreboard
 *     the whole block), less if one instruction is between them.
 *   - stores are posted to the bus controller's queue; what they cost is the
 *     region's wait states once the queue fills.
 *   - a MEMB address with a scaled index (modes 7, 14, 15) takes the address
 *     unit an extra half cycle.
 *   - mulo is the integer multiply's early-out (Tables C-2..C-13), cheaper
 *     than the table's 18 for the operands games use.
 *
 * The costs are not Intel's: those depend on the board's wait states, which
 * the manuals cannot give. They are the owner's measurements on a Model 2B
 * (Pinboard #611-#738): timed loops of each kind, run on the board and read
 * off its own timers, as extras over MAME's table. Each cost is fitted on the
 * bench that measures it alone, and every bench is then held to the model:
 * all but six land within their measurement noise (the rest are #744's
 * residual table; off the benches the costs were fitted on, 0.55 cycles a
 * loop turn RMS). The model has not been fitted to game frames. Two things
 * are assumed, not measured:
 *   - code fetched from RAM costs what code fetched from ROM does. Sega Rally
 *     runs from RAM; every bench ran from ROM. A single-word RAM read costs
 *     what a ROM read does on the board, so the line fill should too.
 *   - a Model 2A's regions cost what the 2B's at the same addresses do, and
 *     the 2A's RAM at 0x200000 and the backup SRAM cost what work RAM does.
 *     Sega Rally keeps much of its race state in both.
 *
 * Costs are kept in 1/1024ths of a cycle (I960_CYC_ONE); g_i960_cyc_frac
 * carries the remainder between instructions. The branch an instruction took
 * is charged at the start of the next one, when its target is known; the
 * cycle count the timers see is one instruction late by those few cycles. */

#define I960_BT(x) ((int32_t)((x) * 1024.0 + ((x) < 0 ? -0.5 : 0.5)))

enum {
    I960_BT_RAM, I960_BT_ROM, I960_BT_DROM, I960_BT_BUF, I960_BT_CSTAT, I960_BT_GEO,
    I960_BT_TMR, I960_BT_FIFO, I960_BT_TILE, I960_BT_IO, I960_BT_REGIONS
};

/* Where an access goes, by the 2B's map (#738's buckets). The 2A's RAM at
 * 0x200000 and the backup SRAM are RAM on MAME's map and are taken to cost
 * what work RAM does; STF touches neither often enough for #738 to fit them. */
static inline int i960_bt_region(uint32_t ea) {
    if (ea >= 0x02000000u) return I960_BT_DROM;              /* data ROM, texture RAM */
    if (ea <  0x00200000u) return I960_BT_ROM;
    if (ea - 0x00500000u < 0x100000u) return I960_BT_RAM;
    if (ea - 0x00200000u < 0x40000u)  return I960_BT_RAM;    /* a 2A's second RAM */
    if (ea - 0x01D00000u < 0x4000u)   return I960_BT_RAM;    /* backup SRAM */
    if (ea - 0x00880000u < 0x10000u)  return I960_BT_FIFO;   /* COP FIFO */
    if (ea - 0x00900000u < 0x80000u)  return I960_BT_BUF;    /* bufferram */
    if (ea - 0x00980000u < 0x40u)     return I960_BT_CSTAT;  /* COP/GEO control, status */
    if (ea - 0x00F00000u < 0x10u)     return I960_BT_TMR;
    if (ea - 0x01000000u < 0x20000u)  return I960_BT_TILE;
    if (ea - 0x00800000u < 0x80000u)  return I960_BT_GEO;
    return I960_BT_IO;
}

/* A load's or store's extra by region and words, [region][words]; filled once. */
static int32_t g_i960_bt_ld[I960_BT_REGIONS][5];
static int32_t g_i960_bt_st[I960_BT_REGIONS][5];

/* The fitted extras (C738.json, in cycles). A region the fit has one width
 * for costs that per word; one it has none for costs what RAM does. */
static inline void i960_bt_init(void) {
    static bool done;
    if (done) return;
    done = true;
    static const double ld_ram[5]  = { 0, 0.3475, 2.1125, 1.64, 1.155 };
    static const double ld_drom[5] = { 0, 2.25, 5.5, 8.75, 12.0 };
    static const double st_ram[5]  = { 0, 2.1866667, 2.3208333, 2.14, 2.8733333 };
    static const double st_bus[5]  = { 0, 1.7341667, 3.7288889, 5.7236111, 7.7183333 };  /* buf, geo */
    static const double st_slow[5] = { 0, 47.1241667, 95.3638889, 143.6036111, 191.8433333 };  /* tile, io */
    static const double ld_word[I960_BT_REGIONS] = {
        [I960_BT_ROM] = 0.25, [I960_BT_BUF] = -0.48875, [I960_BT_CSTAT] = -0.5, [I960_BT_GEO] = -0.5,
        [I960_BT_TMR] = -0.25, [I960_BT_TILE] = 45.265, [I960_BT_IO] = 45.265,
    };
    for (int r = 0; r < I960_BT_REGIONS; r++)
        for (int w = 1; w <= 4; w++) {
            double l = r == I960_BT_RAM ? ld_ram[w] : r == I960_BT_DROM ? ld_drom[w] : ld_word[r] * w;
            double s = r == I960_BT_BUF || r == I960_BT_GEO ? st_bus[w]
                     : r == I960_BT_TILE || r == I960_BT_IO ? st_slow[w]
                     : r == I960_BT_FIFO ? 10.4182292 * w   /* the COP's reply wait (#705) */
                     : st_ram[w];
            g_i960_bt_ld[r][w] = I960_BT(l);
            g_i960_bt_st[r][w] = I960_BT(s);
        }
}

#define I960_BT_SEQMISS  I960_BT(10.0)     /* running on into a line that misses */
#define I960_BT_BRRUN    I960_BT(6.0)      /* ... the line after a branch's missed line */
#define I960_BT_BRMISS   I960_BT(19.875)   /* a branch into a line that misses, at its word 0 */
#define I960_BT_SEQHIT   I960_BT(1.0)      /* running on into another line that hits */
#define I960_BT_SEQHIT_ST I960_BT(0.5633333) /* ... from a line whose long work was stores */
#define I960_BT_TAKEN    I960_BT(1.0)      /* every taken branch, call and return */
#define I960_BT_COBR_NT  I960_BT(-1.125)   /* compare-and-branch not taken */
#define I960_BT_BCC      I960_BT(1.75)     /* conditional branch, taken or not */
#define I960_BT_CALLRET  I960_BT(-0.5)     /* call, callx, calls, ret, bal, balx */
#define I960_BT_MULO     I960_BT(-3.25)
#define I960_BT_USE0     I960_BT(3.5275)   /* reading a register the last load fills */
#define I960_BT_USE1     I960_BT(1.4875)   /* ... the load before that */
#define I960_BT_INDEX    I960_BT(0.50375)  /* a MEMB address with a scaled index */

/* A branch into a missed line by the word it lands on: words 0, 2 and 3 are
 * the jump benches; word 1 is taken halfway between 0 and 2. */
static const int32_t g_i960_bt_brword[4] = {
    0, I960_BT(0.046875), I960_BT(0.09375), I960_BT(0.5625),
};

/* The words a load or store moves, 0 for anything else; bit 1 of the
 * opcode is a store. */
static inline int i960_bt_words(uint32_t op) {
    static const uint8_t words[0x50] = {
        [0x00] = 1, [0x02] = 1, [0x08] = 1, [0x0A] = 1, [0x10] = 1, [0x12] = 1,  /* b, s, w */
        [0x18] = 2, [0x1A] = 2, [0x20] = 3, [0x22] = 3, [0x30] = 4, [0x32] = 4,  /* l, t, q */
        [0x40] = 1, [0x42] = 1, [0x48] = 1, [0x4A] = 1,                          /* ib, is */
    };
    return op - 0x80u < 0x50u ? words[op - 0x80u] : 0;
}

/* n registers from reg up. */
static inline uint32_t i960_bt_block(uint32_t reg, int n) {
    return (uint32_t)((((uint64_t)1 << n) - 1) << reg);
}

/* A MEM instruction's address registers, and a store's data registers. */
static inline uint32_t i960_bt_mem_sources(uint32_t w1) {
    uint32_t s = 0, abase = 1u << ((w1 >> 14) & 31), index = 1u << (w1 & 31);
    unsigned m = (w1 >> 10) & 15;
    if (!((w1 >> 12) & 1)) {                                    /* MEMA */
        if ((w1 >> 13) & 1) s |= abase;
    } else {                                                    /* MEMB */
        if (m == 4 || m == 7 || m == 13 || m == 15) s |= abase;
        if (m == 7 || m == 14 || m == 15) s |= index;
    }
    uint32_t op = w1 >> 24;
    int n = i960_bt_words(op);
    if (n && (op & 2)) s |= i960_bt_block((w1 >> 19) & 31, n);
    return s;
}

/* The registers an instruction reads (a store's data registers too): what
 * the load scoreboard checks. */
static inline uint32_t i960_bt_sources(uint32_t w1) {
    uint32_t op = w1 >> 24;
    if (op >= 0x80) return i960_bt_mem_sources(w1);
    uint32_t s1 = 1u << ((w1 >> 19) & 31), s2 = 1u << ((w1 >> 14) & 31);
    if (op - 0x20u < 0x20u)                                     /* COBR */
        return ((w1 >> 13) & 1 ? 0 : s1) | s2;
    if (op - 0x58u < 0x28u)                                     /* REG */
        return ((w1 >> 11) & 1 ? 0 : 1u << (w1 & 31)) | ((w1 >> 12) & 1 ? 0 : s2);
    return 0;
}

static inline bool i960_bt_uncond(uint32_t op) {
    return op - 0x08u < 4u || op - 0x84u < 4u;     /* b, call, ret, bal; bx, balx, callx, calls */
}

/* The last instruction's branch, now that this one's address says whether it
 * went. An interrupt or a hook's jump counts as one too. */
static inline int32_t i960_bt_branch(i960_bt_t *t, uint32_t ip, bool *taken) {
    *taken = false;
    if (!t->prev_valid) return 0;
    uint32_t op = t->prev_op;
    *taken = ip != t->next_ip || i960_bt_uncond(op);
    int32_t e = *taken ? I960_BT_TAKEN : 0;
    if (op == 0x09 || op == 0x0A || op == 0x0B || op == 0x86 || op == 0x87) {
        if (*taken) e += I960_BT_CALLRET;
    } else if (op - 0x10u < 8u) {
        e += I960_BT_BCC;
    } else if (op - 0x30u < 0x10u) {
        if (!*taken) e += I960_BT_COBR_NT;
    }
    return e;
}

/* Running on into a line that hits: free while the line left held a long
 * instruction (a load, a compare-and-branch, anything of two table cycles or
 * more) the fetch overlaps, part of it after only stores, a cycle otherwise. */
static inline int32_t i960_bt_seqhit(const i960_bt_t *t) {
    return t->line_ld ? 0 : t->line_st ? I960_BT_SEQHIT_ST : I960_BT_SEQHIT;
}

/* A line that misses. A branch's waits for the whole burst, a little longer
 * the later the word it lands on; the line after it waits for its own burst
 * behind that one's. */
static inline int32_t i960_bt_miss(i960_bt_t *t, uint32_t ln, uint32_t ip, bool target) {
    if (target) {
        t->brrun = ln + 2;
        return I960_BT_BRMISS + g_i960_bt_brword[(ip >> 2) & 3];
    }
    int32_t e = I960_BT_SEQMISS + (t->brrun == ln + 1 ? I960_BT_BRRUN : 0);
    t->brrun = 0;
    return e;
}

/* Fetching this instruction's lines through the cache, then noting what it
 * leaves its line holding. */
static inline int32_t i960_bt_fetch(i960_bt_t *t, uint32_t ip, uint32_t len, bool branched,
                                    uint32_t op, unsigned cost) {
    int32_t e = 0;
    uint32_t first = ip >> 4, last = (ip + len - 1) >> 4;
    for (uint32_t ln = first; ; ln++) {
        uint32_t *tag = &t->tag[ln & (I960_BT_LINES - 1)];
        bool target = branched && ln == first;
        if (*tag != ln + 1) {
            *tag = ln + 1;
            e += i960_bt_miss(t, ln, ip, target);
        } else if (target) {
            t->brrun = 0;
        } else if (!t->last_valid || ln != t->last_line) {
            e += i960_bt_seqhit(t);
        }
        if (ln == last) break;
    }
    if (branched || !t->last_valid || first != t->last_line) t->line_ld = t->line_st = 0;
    if (cost >= 2) {
        if (i960_bt_words(op) && (op & 2)) t->line_st = 1;
        else t->line_ld = 1;
    }
    t->last_line = last;
    t->last_valid = 1;
    return e;
}

/* The load scoreboard, and this instruction's own load or store. */
static inline int32_t i960_bt_data(i960_bt_t *t, i960_cpu_t *cpu, uint32_t w1, uint32_t w2) {
    uint32_t op = w1 >> 24;
    uint32_t src = i960_bt_sources(w1);
    int32_t e = I960_BT_USE0 * __builtin_popcount(src & t->pend0)
              + I960_BT_USE1 * __builtin_popcount(src & t->pend1);
    uint32_t older = t->pend0 & ~src;
    t->pend0 = 0;
    t->pend1 = older;
    if (op == 0x70 && ((w1 >> 7) & 15) == 1) e += I960_BT_MULO;
    int n = i960_bt_words(op);
    if (!n) return e;
    uint32_t m = (w1 >> 10) & 15;
    if (((w1 >> 12) & 1) && (m == 7 || m == 14 || m == 15)) e += I960_BT_INDEX;
    int len;
    int r = i960_bt_region(mem_ea(cpu, w1, w2, &len));
    if (op & 2) return e + g_i960_bt_st[r][n];
    uint32_t mask = i960_bt_block((w1 >> 19) & 31, n);
    t->pend0 = mask;
    t->pend1 = older & ~mask;
    return e + g_i960_bt_ld[r][n];
}

/* Charge one instruction: the table's cost and the board's extras. */
static inline void i960_bt_charge(i960_cpu_t *cpu, uint32_t ip, uint32_t w1, uint32_t w2,
                                  unsigned cost) {
    i960_bt_t *t = &g_i960_bt;
    uint32_t op = w1 >> 24;
    uint32_t mode = (w1 >> 10) & 15;
    uint32_t len = op >= 0x80 && ((w1 >> 12) & 1) && mode >= 5 && mode != 6 && mode != 7 ? 8 : 4;
    bool branched;
    int32_t e = i960_bt_branch(t, ip, &branched);
    e += i960_bt_fetch(t, ip, len, branched, op, cost);
    e += i960_bt_data(t, cpu, w1, w2);
    int64_t a = (int64_t)cost * I960_CYC_ONE + e + g_i960_cyc_frac;
    if (a < 0) a = 0;
    cpu->cycles += (uint64_t)a >> 10;
    g_i960_cyc_frac = (uint32_t)(a & (I960_CYC_ONE - 1));
    t->next_ip = ip + len;
    t->prev_op = (uint8_t)op;
    t->prev_valid = 1;
}

#endif /* I960_TIMING_H */
