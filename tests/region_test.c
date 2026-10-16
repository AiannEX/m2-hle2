/*
 * region_test.c -- the board region (g_region, core/hle_hooks.h) on every
 * runnable profile, on a bare memory bus. No ROM.
 *
 * Every profile in registry.h says what it does with the region: a hook that
 * puts g_region in the game's country setting (quirks.region_hook) or why the
 * set has none (quirks.region_fixed). The web page sets the region from its
 * ?lang= (Pinboard #942), so a new profile has to answer. Then the hooks
 * themselves: Sega Rally's country_default writes COUNTRY (0x202019), and
 * STF's settings_loaded writes the working block's country over a saved block
 * only when the region was asked for (g_region_over_saved).
 *
 * A ctest (CMakeLists.txt, region_test).
 */
#define NDEBUG 1
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "memory.h"
#include "i960.h"
#include "emu_thread.h"
#include "registry.h"

static memory_bus_t bus;
static i960_cpu_t   cpu;
static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); g_fail++; } \
                              else { printf("ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static hle_hook_fn hook_named(const game_profile_t *p, const char *name) {
    for (int i = 0; i < p->hook_count; i++)
        if (p->hooks[i].name && !strcmp(p->hooks[i].name, name)) return p->hooks[i].fn;
    return NULL;
}

static void check_declared(void) {
    for (size_t i = 0; i < g_profile_count; i++) {
        const game_profile_t *p = g_profiles[i];
        int hook = p->quirks.region_hook, fixed = p->quirks.region_fixed && p->quirks.region_fixed[0];
        CHECK(hook != fixed, "%s: region_hook or region_fixed, one of them", p->id);
    }
}

static void check_srallyc(void) {
    hle_hook_fn fn = hook_named(&srallyc_profile, "country_default");
    CHECK(fn != NULL, "srallyc: country_default hook");
    if (!fn) return;
    for (int r = GAME_REGION_JAPAN; r <= GAME_REGION_EXPORT; r++) {
        g_region = r;
        mem_write8(&bus, 0x00202019u, 0);
        int run = fn(&cpu, &bus);
        CHECK(run == 1 && mem_read8(&bus, 0x00202019u) == (uint32_t)r,
              "srallyc: region %d -> COUNTRY %d, the instruction runs", r, r);
    }
}

static void check_sfight(void) {
    hle_hook_fn fn = hook_named(&sfight_profile, "settings_loaded");
    CHECK(fn != NULL, "sfight: settings_loaded hook");
    CHECK(hook_named(&sfight_console_profile, "settings_loaded") == fn, "sfight_console: the same hook");
    if (!fn) return;
    const uint32_t country = SFIGHT_SETTINGS_WORK + 0x12u;
    mem_write32(&bus, SFIGHT_SETTINGS_PTR, 0x00599000u);
    g_region = GAME_REGION_EXPORT;

    g_region_over_saved = 0;
    mem_write8(&bus, country, GAME_REGION_USA);
    CHECK(fn(&cpu, &bus) == 1 && mem_read8(&bus, country) == GAME_REGION_USA,
          "sfight: a saved country stays when the region was not asked for");

    g_region_over_saved = 1;
    CHECK(fn(&cpu, &bus) == 1 && mem_read8(&bus, country) == GAME_REGION_EXPORT,
          "sfight: the asked-for region goes over the saved country");
    g_region_over_saved = 0;
}

int main(void) {
    if (!mem_init(&bus, NULL, 0)) { printf("mem_init failed\n"); return 1; }
    i960_reset(&cpu);
    check_declared();
    check_srallyc();
    check_sfight();
    printf(g_fail ? "\nFAILED (%d)\n" : "\nPASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
