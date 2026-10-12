/*
 * prepared_test.c — the prepared profiles (registry.h, g_prepared_profiles;
 * Pinboard #660): Virtua Cop, Daytona USA and The House of the Dead, which no
 * board here can run yet.
 *
 * Without ROMs (the canary): every recipe part lands inside a region the
 * recipe gives a size, every profile says what its board lacks and has no
 * installer, and the lookup finds them by set name.
 *
 * With a set in $ROMDIR / $ROMS_DIR: it loads, the PRCB and the start IP are
 * inside the program, ROM_COPY mirrors match their source, and the model table
 * the profile names (noclip's, js/games.js) points into the polygon ROM. A set
 * that is not there is skipped.
 */
#define NDEBUG 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "vcop.h"
#include "daytona.h"
#include "hotd.h"

#include "test_rom_dir.h"

const game_profile_t *const g_prepared_profiles[] = { &vcop_profile, &daytona_profile, &hotd_profile };
const size_t g_prepared_count = sizeof g_prepared_profiles / sizeof g_prepared_profiles[0];

static int g_fail = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL: "); g_fail++; } else printf("ok:   "); \
    printf(__VA_ARGS__); printf("\n"); \
} while (0)

/* What the test reads a profile with: its recipe, and noclip's model-table
 * entry layout (js/games.js, js/romset.js readModelEntry). The 1995 games keep
 * the mesh pointer at +8; Daytona and Virtua Cop put it first. */
typedef struct {
    const game_profile_t *p;
    const rom_recipe_t   *recipe;
    uint32_t              stride, mesh_field;
} prepared_case_t;

static const prepared_case_t k_cases[] = {
    { &vcop_profile,    &vcop_recipe,    16, 0 },
    { &daytona_profile, &daytona_recipe, 20, 0 },
    { &hotd_profile,    &hotd_recipe,    16, 8 },
};

static uint32_t rd32(const uint8_t *b, size_t at) {
    return (uint32_t)b[at] | (uint32_t)b[at + 1] << 8 | (uint32_t)b[at + 2] << 16 | (uint32_t)b[at + 3] << 24;
}

static void check_recipe(const rom_recipe_t *rc) {
    int bad = 0;
    for (size_t i = 0; i < rc->count; i++) {
        const rom_part_t *pt = &rc->parts[i];
        uint32_t size = rc->size[pt->region];
        uint32_t end = pt->kind == ROM_PART_COPY ? pt->offset + pt->len : pt->offset + 1;
        if (!size || end > size || (pt->kind == ROM_PART_COPY && pt->from + pt->len > size)) {
            printf("      part %zu (%s) is outside its region\n", i, pt->name ? pt->name : "mirror");
            bad++;
        }
    }
    CHECK(bad == 0, "%s: every part lands inside a sized region", rc->set);
}

static void check_profile(const prepared_case_t *c) {
    const game_profile_t *p = c->p;
    CHECK(p->load_fn && !p->install_fn, "%s: loads, and has no installer", p->id);
    CHECK(p->preliminary && p->preliminary[0], "%s: says what the board lacks", p->id);
    CHECK(profile_prepared_for_rom_set(p->id) == p, "%s: found by its set name", p->id);
    char msg[256];
    CHECK(profile_prepared_message(p->id, msg, sizeof msg) && strstr(msg, p->display_name),
          "%s: the front ends' message names it", p->id);
    check_recipe(c->recipe);
}

/* The program's reset structures: word 1 is the PRCB, PRCB + 4 the start IP. */
static void check_program(const char *id, const romset_t *rs) {
    uint32_t prcb = rd32(rs->maincpu, 4);
    CHECK(prcb && prcb + 8 <= rs->maincpu_size, "%s: PRCB 0x%X inside the program", id, prcb);
    if (prcb + 8 > rs->maincpu_size) return;
    uint32_t ip = rd32(rs->maincpu, prcb + PRCB_START_IP);
    CHECK(ip && ip < rs->maincpu_size, "%s: start IP 0x%X inside the program", id, ip);
}

static void check_mirrors(const char *id, const rom_recipe_t *rc, romset_t *rs) {
    int bad = 0, n = 0;
    for (size_t i = 0; i < rc->count; i++) {
        const rom_part_t *pt = &rc->parts[i];
        if (pt->kind != ROM_PART_COPY) continue;
        size_t *sz;
        uint8_t **reg = rom_recipe_region(rs, pt->region, &sz);
        n++;
        if (memcmp(*reg + pt->from, *reg + pt->offset, pt->len) != 0) bad++;
    }
    if (n) CHECK(bad == 0, "%s: %d mirrors match their source", id, n);
}

/* Entries whose mesh pointer is set must decode (pointer * 4 - subtract + add,
 * romset.js meshOffsetOf) to inside the polygon ROM. A pointer with bit 23
 * clear is polygon RAM, which the game fills at run time (MAME
 * geo_object_data): Daytona has 27 of those, and they are counted apart. */
static void check_model_table(const prepared_case_t *c, const romset_t *rs) {
    const game_quirks_t *q = &c->p->quirks;
    uint64_t last = q->model_table_offset + (uint64_t)q->model_table_count * c->stride;
    CHECK(last <= rs->main_data_size, "%s: model table inside main_data", c->p->id);
    if (last > rs->main_data_size) return;
    uint32_t set = 0, inside = 0, ram = 0;
    for (uint32_t i = 0; i < q->model_table_count; i++) {
        uint32_t ptr = rd32(rs->main_data, q->model_table_offset + i * c->stride + c->mesh_field);
        if (!ptr) continue;
        set++;
        if (!(ptr & 0x800000)) { ram++; continue; }
        uint32_t off = ptr * 4 - q->mesh_ptr_subtract + q->mesh_ptr_add;
        if (off < rs->polygons_size) inside++;
    }
    printf("info: %s: %u of %u models have a mesh: %u in the polygon ROM, %u in polygon RAM\n",
           c->p->id, set, q->model_table_count, inside, ram);
    CHECK(set > q->model_table_count / 2 && inside + ram == set, "%s: model table points into the polygon ROM", c->p->id);
}

static void check_set(const prepared_case_t *c) {
    const char *zip = test_rom(c->p->id), *id = c->p->id;
    char path[1100];
    snprintf(path, sizeof path, "%s.zip", zip);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("skip: %s not found\n", path); return; }
    fclose(f);
    static romset_t rs;
    int rc = c->p->load_fn(&rs, path, NULL);
    CHECK(rc == 0 && rs.loaded, "%s: the set loads", id);
    if (rc != 0) return;
    for (int r = 0; r < ROM_REGION_COUNT; r++) {
        size_t *sz;
        rom_recipe_region(&rs, (rom_region_id_t)r, &sz);
        if (*sz != c->recipe->size[r]) CHECK(0, "%s: region %d is 0x%zX, not 0x%X", id, r, *sz, c->recipe->size[r]);
    }
    check_program(id, &rs);
    check_mirrors(id, c->recipe, &rs);
    check_model_table(c, &rs);
    romset_free(&rs);
}

int main(void) {
    for (size_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++) check_profile(&k_cases[i]);
    char msg[64];
    CHECK(!profile_prepared_for_rom_set("sfight") && !profile_prepared_message("sfight", msg, sizeof msg),
          "a runnable set is not a prepared one");
    for (size_t i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++) check_set(&k_cases[i]);
    printf(g_fail ? "\nFAILED (%d)\n" : "\nPASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
