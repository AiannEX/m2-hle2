/*
 * model_table.h — the model table: where a game keeps, per model, the three
 * addresses its draw hands the geometrizer (the object command's oba, tpa
 * and tha: the mesh in polygon ROM, the UV stream and the face records).
 *
 * The board reads the three from the object command, so the table's shape is
 * the game's. Three are known, the same three noclip reads (js/romset.js
 * readModelEntry, js/games.js modelTable.stride / fields; NOCLIP-SYNC.md §6.3):
 *
 *   1995 (STF, FV, HOTD, the homebrew)  16 bytes: uv +0, mat +4, mesh +8
 *   Virtua Cop                          16 bytes: mesh +0, uv +4, mat +8, count
 *   Daytona USA                         20 bytes: Virtua Cop's, and a 0 ending
 *                                       the one-entry object list
 *
 * A layout of all zeros is the 1995 one, so a profile names one only when it
 * differs.
 */
#ifndef M2HLE_MODEL_TABLE_H
#define M2HLE_MODEL_TABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Byte offsets within an entry, and the entry's size. */
typedef struct {
    uint8_t stride, uv, mat, mesh;
} model_layout_t;

#define MODEL_LAYOUT_1995    ((model_layout_t){ .stride = 16, .uv = 0, .mat = 4, .mesh = 8 })
#define MODEL_LAYOUT_VCOP    ((model_layout_t){ .stride = 16, .uv = 4, .mat = 8, .mesh = 0 })
#define MODEL_LAYOUT_DAYTONA ((model_layout_t){ .stride = 20, .uv = 4, .mat = 8, .mesh = 0 })

/* Where the table is in main_data, how many entries, and their layout. */
typedef struct {
    uint32_t       off, count;
    model_layout_t layout;
} model_table_t;

/* One entry's three words, as the ROM has them: uv_ptr a word index into the
 * textures ROM (byte = *2), mat_ptr a word address (bit 23: texture RAM),
 * mesh_ptr the encoded object address (bit 23 set: polygon ROM). */
typedef struct {
    uint32_t uv_ptr, mat_ptr, mesh_ptr;
} model_entry_t;

static inline model_table_t model_table_make(uint32_t off, uint32_t count, model_layout_t layout) {
    return (model_table_t){ off, count, layout.stride ? layout : MODEL_LAYOUT_1995 };
}

static inline uint32_t model_table__rd32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Byte offset in main_data of entry idx. */
static inline uint32_t model_entry_offset(const model_table_t *t, uint32_t idx) {
    return t->off + idx * t->layout.stride;
}

/* Whether entries [0, count) lie inside main_data. */
static inline bool model_table_fits(const model_table_t *t, size_t main_data_size) {
    return (uint64_t)t->off + (uint64_t)t->count * t->layout.stride <= main_data_size;
}

/* Entry idx; false past the table or past main_data (the entry is zeroed). */
static inline bool model_entry_read(const model_table_t *t, const uint8_t *main_data, size_t main_data_size,
                                    uint32_t idx, model_entry_t *e) {
    *e = (model_entry_t){ 0, 0, 0 };
    if (!main_data || idx >= t->count) return false;
    uint64_t at = model_entry_offset(t, idx);
    if (at + t->layout.stride > main_data_size) return false;
    const uint8_t *p = main_data + at;
    e->uv_ptr   = model_table__rd32(p + t->layout.uv);
    e->mat_ptr  = model_table__rd32(p + t->layout.mat);
    e->mesh_ptr = model_table__rd32(p + t->layout.mesh);
    return true;
}

/* An object address with bit 23 set is a word index into the polygon ROM
 * (MAME geo_object_data); without it, polygon RAM, which the game fills at run
 * time. Daytona's entries 2139-2165 are of the second kind. */
static inline bool model_mesh_in_rom(uint32_t mesh_ptr) {
    return (mesh_ptr & 0x00800000u) != 0;
}

#endif /* M2HLE_MODEL_TABLE_H */
