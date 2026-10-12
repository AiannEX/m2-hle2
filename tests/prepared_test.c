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
 * the profile names (noclip's, js/games.js) points into the polygon ROM and,
 * read through the profile's layout (model_table.h; Pinboard #663), gives what
 * noclip's readModelEntry gives; and each ROM-backed entry's mesh resolves
 * (model_mesh_resolve; Pinboard #666) to the polygon-ROM offset noclip's
 * meshOffsetOf gives, with the first record decodeModel reads; and each of
 * those meshes, decoded (geo3d_mesh_reads; Pinboard #669), walks the records,
 * reads the texture headers and points and comes to the faces and triangles
 * noclip's decodeModel does. A set that is not there is skipped.
 */
#define NDEBUG 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "constants.h"
#include "vcop.h"
#include "daytona.h"
#include "hotd.h"
#include "geo3d.h"

#include "test_rom_dir.h"

const game_profile_t *const g_prepared_profiles[] = { &vcop_profile, &daytona_profile, &hotd_profile };
const size_t g_prepared_count = sizeof g_prepared_profiles / sizeof g_prepared_profiles[0];

static int g_fail = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL: "); g_fail++; } else printf("ok:   "); \
    printf(__VA_ARGS__); printf("\n"); \
} while (0)

/* What noclip reads from a set's model table (js/romset.js readModelEntry,
 * with the stride and fields js/games.js gives the game): the entry count,
 * FNV-1a over every entry's uv, mat and mesh words (little-endian, in that
 * order), the meshes set and the ones in polygon RAM, and three entries.
 * Taken with node from the noclip tree (loadRomSet + readModelEntry) on the
 * container's vcop.zip, daytona.zip and hotd.zip. */
typedef struct {
    uint32_t idx, off, attr;
} noclip_mesh_t;

typedef struct {
    uint32_t      count, fnv, meshes, ram;
    uint32_t      sample_idx[3];
    model_entry_t sample[3];
    /* Where noclip finds each ROM-backed entry's mesh (js/romset.js
     * meshOffsetOf; js/model.js decodeModel, which takes it only if the first
     * 40-byte record is inside the polygon ROM, and reads that record's
     * points at +0..+20, attribute at +24 and normal at +28..+36): the meshes
     * found, the ones past the ROM, FNV-1a over each one's index, offset and
     * ten record words, and three of them. Polygon-RAM entries are skipped.
     * Taken with node from noclip's master (aefb324). */
    uint32_t      mesh_rom, mesh_past, mesh_fnv;
    noclip_mesh_t mesh[3];
    /* decodeModel on each of those meshes (js/model.js), with the polygon and
     * textures ROMs wrapped to log every index read: the meshes decoded, the
     * ones it returns null for (no triangle), the records, faces and textured
     * faces summed, and FNV-1a, per mesh, over its index, offset, records, the
     * count and indices of the textures-ROM bytes read, then 0xFFFFFFFF for
     * null or faceCount, texturedFaces, paletteMax, the triangle count and
     * each triangle's face. Taken with node from noclip's master (aefb324). */
    uint32_t      dec_n, dec_null, dec_records, dec_faces, dec_textured, dec_fnv;
} noclip_reads_t;

/* What the test reads a profile with: its recipe, the layout its quirks must
 * name (model_table.h), and noclip's reads. */
typedef struct {
    const game_profile_t *p;
    const rom_recipe_t   *recipe;
    model_layout_t        layout;
    noclip_reads_t        noclip;
} prepared_case_t;

static const prepared_case_t k_cases[] = {
    { &vcop_profile, &vcop_recipe, MODEL_LAYOUT_VCOP,
      { 1234, 0x2298d18du, 1234, 0, { 0, 1, 1233 },
        { { 0x50000, 0x0, 0x800004 }, { 0x50760, 0x3cc, 0x8009f1 }, { 0xc292a, 0x39240, 0x89c275 } },
        1234, 0, 0xab8c6c68u, { { 0, 0x10, 0x1841701 }, { 1, 0x27c4, 0x1841b01 }, { 1233, 0x2709d4, 0x9c1502 } },
        1234, 0, 61375, 57495, 57002, 0xae157b11u } },
    { &daytona_profile, &daytona_recipe, MODEL_LAYOUT_DAYTONA,
      { 3190, 0xa7bff01du, 3190, 27, { 0, 1, 3189 },
        { { 0x17ecd0, 0x117080, 0x880004 }, { 0x17f13e, 0x11728c, 0x8805af }, { 0x85abc, 0x18510, 0x84881b } },
        3163, 0, 0xac46f303u, { { 0, 0x200010, 0x941501 }, { 1, 0x2016bc, 0x89141601 }, { 3189, 0x12206c, 0x41401 } },
        3163, 813, 203300, 163823, 138791, 0x5195b8e6u } },
    { &hotd_profile, &hotd_recipe, MODEL_LAYOUT_1995,
      { 7477, 0xac1eb34fu, 7465, 0, { 0, 1, 7476 },
        { { 0x0, 0x6, 0x800000 }, { 0x0, 0x6, 0x800000 }, { 0x6c7ffa, 0x6c821a, 0xef296a } },
        7465, 0, 0xf2dfe35cu, { { 0, 0x0, 0x1000a02 }, { 1, 0x0, 0x1000a02 }, { 7476, 0x1bca5a8, 0x1000601 } },
        7465, 0, 466634, 378760, 367185, 0xdc922fa1u } },
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
    const model_table_t t = profile_model_table(&p->quirks);
    CHECK(!memcmp(&t.layout, &c->layout, sizeof t.layout),
          "%s: model table entries are %u bytes, mesh +%u uv +%u mat +%u", p->id,
          t.layout.stride, t.layout.mesh, t.layout.uv, t.layout.mat);
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

static uint32_t fnv_word(uint32_t h, uint32_t v) {
    for (int b = 0; b < 4; b++) { h ^= (v >> (b * 8)) & 0xff; h *= 0x01000193u; }
    return h;
}

/* Every entry read through the profile's table must match noclip's reads. */
static void check_noclip_reads(const char *id, const model_table_t *t, const romset_t *rs,
                               const noclip_reads_t *n) {
    uint32_t h = 0x811c9dc5u;
    model_entry_t e;
    for (uint32_t i = 0; i < t->count; i++) {
        model_entry_read(t, rs->main_data, rs->main_data_size, i, &e);
        h = fnv_word(fnv_word(fnv_word(h, e.uv_ptr), e.mat_ptr), e.mesh_ptr);
    }
    CHECK(t->count == n->count && h == n->fnv, "%s: %u entries, FNV 0x%08X (noclip: %u, 0x%08X)",
          id, t->count, h, n->count, n->fnv);
    int bad = 0;
    for (int k = 0; k < 3; k++) {
        model_entry_read(t, rs->main_data, rs->main_data_size, n->sample_idx[k], &e);
        if (memcmp(&e, &n->sample[k], sizeof e)) {
            printf("      entry %u: uv=%X mat=%X mesh=%X\n", n->sample_idx[k], e.uv_ptr, e.mat_ptr, e.mesh_ptr);
            bad++;
        }
    }
    CHECK(bad == 0, "%s: entries %u, %u and %u are noclip's", id,
          n->sample_idx[0], n->sample_idx[1], n->sample_idx[2]);
}

/* Each ROM-backed entry's mesh, found through model_mesh_resolve, must be
 * where noclip finds it and open on the same record. */
static void check_noclip_meshes(const char *id, const model_table_t *t, const game_quirks_t *q,
                                const romset_t *rs, const noclip_reads_t *n) {
    uint32_t h = 0x811c9dc5u, found = 0, past = 0, off;
    model_entry_t e;
    for (uint32_t i = 0; i < t->count; i++) {
        model_entry_read(t, rs->main_data, rs->main_data_size, i, &e);
        if (!e.mesh_ptr || !model_mesh_in_rom(e.mesh_ptr)) continue;
        if (!model_mesh_resolve(e.mesh_ptr, q->mesh_ptr_subtract, q->mesh_ptr_add, rs->polygons_size, &off)) {
            past++;
            continue;
        }
        found++;
        h = fnv_word(fnv_word(h, i), off);
        for (int w = 0; w < 10; w++) h = fnv_word(h, rd32(rs->polygons, off + w * 4u));
    }
    CHECK(found == n->mesh_rom && past == n->mesh_past && h == n->mesh_fnv,
          "%s: %u meshes in the polygon ROM, %u past it, first records FNV 0x%08X (noclip: %u, %u, 0x%08X)",
          id, found, past, h, n->mesh_rom, n->mesh_past, n->mesh_fnv);
    int bad = 0;
    for (int k = 0; k < 3; k++) {
        const noclip_mesh_t *m = &n->mesh[k];
        model_entry_read(t, rs->main_data, rs->main_data_size, m->idx, &e);
        bool ok = model_mesh_resolve(e.mesh_ptr, q->mesh_ptr_subtract, q->mesh_ptr_add, rs->polygons_size, &off);
        if (!ok || off != m->off || rd32(rs->polygons, off + 24) != m->attr) {
            printf("      entry %u: mesh@0x%X attr=0x%X\n", m->idx, off, ok ? rd32(rs->polygons, off + 24) : 0);
            bad++;
        }
    }
    CHECK(bad == 0, "%s: entries %u, %u and %u's meshes are at noclip's offsets, with its attributes", id,
          n->mesh[0].idx, n->mesh[1].idx, n->mesh[2].idx);
}

/* What decoding one mesh adds to the totals and the FNV, as the node script
 * mixes it (noclip_reads_t). */
typedef struct {
    uint32_t n, null, records, faces, textured, fnv;
} decode_totals_t;

static void decode_mix(decode_totals_t *d, uint32_t i, uint32_t off, const geo3d_mesh_reads_t *r) {
    uint32_t h = fnv_word(fnv_word(fnv_word(d->fnv, i), off), (uint32_t)r->records);
    h = fnv_word(h, (uint32_t)r->n_reads);
    for (int k = 0; k < r->n_reads; k++) h = fnv_word(h, r->reads[k]);
    d->n++;
    d->records += (uint32_t)r->records;
    if (!r->n_tris) {
        d->null++;
        d->fnv = fnv_word(h, 0xFFFFFFFFu);
        return;
    }
    h = fnv_word(fnv_word(fnv_word(h, (uint32_t)r->faces), (uint32_t)r->textured), (uint32_t)r->palette_max);
    h = fnv_word(h, (uint32_t)r->n_tris);
    for (int k = 0; k < r->n_tris; k++) h = fnv_word(h, r->tri_face[k]);
    d->faces += (uint32_t)r->faces;
    d->textured += (uint32_t)r->textured;
    d->fnv = h;
}

/* Each ROM-backed mesh decoded as geo3d_decode_model walks it (geo3d_mesh_reads)
 * must read what noclip's decodeModel reads and come to the same faces.
 * Polygon-RAM entries (Daytona's 2139-2165) have no ROM mesh and are skipped. */
static void check_noclip_decode(const char *id, const model_table_t *t, const game_quirks_t *q,
                                const romset_t *rs, const noclip_reads_t *n) {
    enum { FACES = GEO3D_IA_MAX_IDX / 4, READS = FACES * 24, TRIS = FACES * 2 };
    static uint32_t reads[READS], tris[TRIS];
    const geo3d_models_t md = {
        .main_data = rs->main_data, .main_data_size = rs->main_data_size,
        .polygons  = rs->polygons,  .polygons_size  = rs->polygons_size,
        .materials = rs->textures,  .materials_size = rs->textures_size,
        .table = *t, .mesh_ptr_subtract = q->mesh_ptr_subtract, .mesh_ptr_add = q->mesh_ptr_add,
    };
    geo3d_mesh_reads_t r = { .reads = reads, .reads_cap = READS, .tri_face = tris, .tris_cap = TRIS };
    decode_totals_t d = { .fnv = 0x811c9dc5u };
    int over = 0;
    model_entry_t e;
    uint32_t off;
    for (uint32_t i = 0; i < t->count; i++) {
        model_entry_read(t, rs->main_data, rs->main_data_size, i, &e);
        if (!model_mesh_resolve(e.mesh_ptr, q->mesh_ptr_subtract, q->mesh_ptr_add, rs->polygons_size, &off))
            continue;
        if (!geo3d_mesh_reads(&md, (int)i, &r)) continue;
        if (r.n_reads > READS || r.n_tris > TRIS) over++;
        decode_mix(&d, i, off, &r);
    }
    CHECK(over == 0, "%s: every decode's reads fit the test's buffers", id);
    CHECK(d.n == n->dec_n && d.null == n->dec_null && d.records == n->dec_records,
          "%s: %u meshes decode, %u to nothing, over %u records (noclip: %u, %u, %u)",
          id, d.n, d.null, d.records, n->dec_n, n->dec_null, n->dec_records);
    CHECK(d.faces == n->dec_faces && d.textured == n->dec_textured,
          "%s: %u faces, %u textured (noclip: %u, %u)", id, d.faces, d.textured, n->dec_faces, n->dec_textured);
    CHECK(d.fnv == n->dec_fnv, "%s: texture reads, palettes and triangles FNV 0x%08X (noclip: 0x%08X)",
          id, d.fnv, n->dec_fnv);
}

/* Entries whose mesh pointer is set must resolve (model_mesh_resolve, noclip's
 * meshOffsetOf) to inside the polygon ROM. A pointer with bit 23 clear is
 * polygon RAM, which the game fills at run time (MAME geo_object_data):
 * Daytona has 27 of those (entries 2139-2165), and they are counted apart,
 * not read as ROM. */
static void check_model_table(const prepared_case_t *c, const romset_t *rs) {
    const game_quirks_t *q = &c->p->quirks;
    const model_table_t t = profile_model_table(q);
    CHECK(model_table_fits(&t, rs->main_data_size), "%s: model table inside main_data", c->p->id);
    if (!model_table_fits(&t, rs->main_data_size)) return;
    uint32_t set = 0, inside = 0, ram = 0, off;
    model_entry_t e;
    for (uint32_t i = 0; i < t.count; i++) {
        model_entry_read(&t, rs->main_data, rs->main_data_size, i, &e);
        if (!e.mesh_ptr) continue;
        set++;
        if (!model_mesh_in_rom(e.mesh_ptr)) { ram++; continue; }
        if (model_mesh_resolve(e.mesh_ptr, q->mesh_ptr_subtract, q->mesh_ptr_add, rs->polygons_size, &off))
            inside++;
    }
    printf("info: %s: %u of %u models have a mesh: %u in the polygon ROM, %u in polygon RAM\n",
           c->p->id, set, t.count, inside, ram);
    CHECK(set > t.count / 2 && inside + ram == set, "%s: model table points into the polygon ROM", c->p->id);
    CHECK(set == c->noclip.meshes && ram == c->noclip.ram, "%s: %u meshes, %u in polygon RAM, as noclip counts",
          c->p->id, c->noclip.meshes, c->noclip.ram);
    check_noclip_reads(c->p->id, &t, rs, &c->noclip);
    check_noclip_meshes(c->p->id, &t, q, rs, &c->noclip);
    check_noclip_decode(c->p->id, &t, q, rs, &c->noclip);
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
