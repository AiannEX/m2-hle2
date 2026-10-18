/*
 * controls_window.h -- the desktop's Controls window (Pinboard #1147): which
 * devices play besides the keyboard, the keyboard's keys, the gamepad's
 * buttons and the racing wheel's axes and buttons, kept per profile.
 *
 * Each profile's choice lives in controls/<profile id>.cfg beside the
 * per-user netplay settings (%APPDATA%\m2hle2, ~/.config/m2hle2), written on
 * every change; a profile without one plays the built-in map:
 *
 *   device=any|keyboard|gamepad|wheel  (keyboard: no pad or wheel plays)
 *   key.<action>=<key>[,<key>]         (p1_up .. p2_coin, service, test;
 *                                       input_keycode_by_name's names; none)
 *   pad.<button>=<actions>             (south east west north start back l1 r1
 *                                       l3 r3 guide; b1+b2, start, service, none)
 *   wheel.axes=<steer>,<accel>,<brake> (joystick axis numbers)
 *   wheel.<n>=<actions>                (joystick button n, 0-31)
 *
 * The map is the host's only (input.h, sdl_pads.h): the board sees actions, so
 * nothing here changes what a session or a replay sends. --pad-map,
 * --wheel-axes and --wheel-buttons apply over the file for that run; the first
 * change made here drops them. --headless reads no file.
 */
#ifndef CONTROLS_WINDOW_H
#define CONTROLS_WINDOW_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cimgui.h"
#include "sokol_app.h"

#include "backup_ram.h"     /* backup_ram_user_dir */
#include "game_profile.h"
#include "input.h"
#include "log.h"
#if M2HLE_PADS
#include "sdl_pads.h"
#endif

#define CONTROLS_CLI_MAX 8

static struct {
    const game_profile_t *loaded;   /* whose controls are in; NULL: none yet */
    int  cap_action, cap_slot;      /* the key slot waiting for a key; -1: none */
    int  swallow;                   /* the captured key, whose release the game must not see */
    char status[160];
    struct { char opt[24], val[256]; } cli[CONTROLS_CLI_MAX];   /* --pad-map and the like */
    int  cli_n;
} g_ctl = { .cap_action = -1 };

/* The words the window shows for the actions. */
static const char *const g_ctl_action_label[GAME_INPUT_COUNT] = {
    "Up", "Down", "Left", "Right", "Button 1", "Button 2", "Button 3", "Button 4", "Start", "Coin",
    "Up", "Down", "Left", "Right", "Button 1", "Button 2", "Button 3", "Button 4", "Start", "Coin",
    "Service", "Test",
};

/* A pad option from the command line, applied again over each profile's file. */
static inline void controls_cli_note(const char *opt, const char *val) {
    if (g_ctl.cli_n >= CONTROLS_CLI_MAX) return;
    snprintf(g_ctl.cli[g_ctl.cli_n].opt, sizeof g_ctl.cli[0].opt, "%s", opt);
    snprintf(g_ctl.cli[g_ctl.cli_n].val, sizeof g_ctl.cli[0].val, "%s", val);
    g_ctl.cli_n++;
}

/* <user dir>/controls/<id>.cfg for profile p; false if there is no user dir. */
static inline bool controls_path(const game_profile_t *p, char *out, size_t cap) {
    char dir[512];
    if (!p || !backup_ram_user_dir(dir, sizeof dir)) return false;
    size_t n = strlen(dir);
    snprintf(dir + n, sizeof dir - n, BACKUP_RAM_SEP "controls");
    backup_ram_mkdir(dir);
    snprintf(out, cap, "%s" BACKUP_RAM_SEP "%s.cfg", dir, p->id);
    return true;
}

static inline void controls_defaults(void) {
    input_keys_default();
#if M2HLE_PADS
    sdl_pads_default();
#endif
}

/* One line of a controls file. */
static inline void controls_apply_line(char *line) {
    char *eq = strchr(line, '=');
    if (line[0] == '#' || !eq) return;
    *eq = '\0';
    const char *k = line, *v = eq + 1;
    if (!strncmp(k, "key.", 4)) {
        int a = input_action_by_name(k + 4);
        if (a < 0) return;
        char buf[64];
        snprintf(buf, sizeof buf, "%s", v);
        int s = 0;
        for (char *tok = strtok(buf, ","); tok && s < INPUT_KEY_SLOTS; tok = strtok(NULL, ",")) {
            int kc = input_keycode_by_name(tok);
            if (kc > 0) g_input_keys[a][s++] = kc;
        }
        while (s < INPUT_KEY_SLOTS) g_input_keys[a][s++] = 0;
        return;
    }
#if M2HLE_PADS
    if (!strcmp(k, "device")) {
        for (int d = 0; d < PAD_DEVICE_COUNT; d++) if (!strcmp(v, g_pad_device_names[d])) g_pad_device = d;
    } else if (!strncmp(k, "pad.", 4)) {
        uint32_t acts;
        for (int b = 0; b < PAD_BIND_COUNT; b++)
            if (!strcmp(k + 4, g_pad_binds[b].name) && actions_by_name(v, &acts)) g_pad_binds[b].acts = acts;
    } else if (!strcmp(k, "wheel.axes")) {
        int st, ac, br;
        if (sscanf(v, "%d,%d,%d", &st, &ac, &br) == 3 && st >= 0 && ac >= 0 && br >= 0) {
            g_wheel_opt.steer = st; g_wheel_opt.accel = ac; g_wheel_opt.brake = br;
        }
    } else if (!strncmp(k, "wheel.", 6)) {
        uint32_t acts;
        int b = atoi(k + 6);
        if (b >= 0 && b < WHEEL_BUTTONS && actions_by_name(v, &acts)) g_wheel_opt.button[b] = acts;
    }
#endif
}

/* Profile p's controls in: the built-in map, its file over that, the command
 * line over both. */
static inline void controls_load(const game_profile_t *p) {
    controls_defaults();
    g_ctl.loaded = p;
    char path[640], line[320];
    FILE *f = controls_path(p, path, sizeof path) ? fopen(path, "r") : NULL;
    if (f) {
        while (fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = '\0';
            controls_apply_line(line);
        }
        fclose(f);
        LOG_INFO("controls: %s", path);
    }
#if M2HLE_PADS
    for (int i = 0; i < g_ctl.cli_n; i++) sdl_pads_option(g_ctl.cli[i].opt, g_ctl.cli[i].val);
#endif
}

static inline void controls_save(void) {
    char path[640], name[16], acts[64];
    if (!controls_path(g_ctl.loaded, path, sizeof path)) return;
    FILE *f = fopen(path, "w");
    if (!f) {
        LOG_WARN("controls: cannot write %s", path);
        snprintf(g_ctl.status, sizeof g_ctl.status, "Cannot write the controls file (see m2hle.log).");
        return;
    }
    fprintf(f, "# m2hle controls for %s\n", g_ctl.loaded->display_name);
#if M2HLE_PADS
    fprintf(f, "device=%s\n", g_pad_device_names[g_pad_device]);
#endif
    for (int a = 0; a < GAME_INPUT_COUNT; a++) {
        fprintf(f, "key.%s=", g_input_action_names[a]);
        int n = 0;
        for (int s = 0; s < INPUT_KEY_SLOTS; s++)
            if (g_input_keys[a][s] && input_keycode_name(g_input_keys[a][s], name))
                fprintf(f, "%s%s", n++ ? "," : "", name);
        fprintf(f, "%s\n", n ? "" : "none");
    }
#if M2HLE_PADS
    for (int b = 0; b < PAD_BIND_COUNT; b++)
        fprintf(f, "pad.%s=%s\n", g_pad_binds[b].name, input_combo_format(g_pad_binds[b].acts, acts, sizeof acts));
    fprintf(f, "wheel.axes=%d,%d,%d\n", g_wheel_opt.steer, g_wheel_opt.accel, g_wheel_opt.brake);
    for (int b = 0; b < WHEEL_BUTTONS; b++)
        if (g_wheel_opt.button[b])
            fprintf(f, "wheel.%d=%s\n", b, input_combo_format(g_wheel_opt.button[b], acts, sizeof acts));
#endif
    fclose(f);
}

/* Before the window changes anything: the command line's pad options were for
 * this run's start, and what is saved is the file's map with this change. */
static inline void controls_edit_begin(void) {
    if (!g_ctl.cli_n) return;
    g_ctl.cli_n = 0;
    controls_load(g_ctl.loaded);
}

/* The pads' presses again after a change, and the force off the ones that
 * stopped playing (ffb_update gives it back to the rest). */
static inline void controls_pads_changed(bool pads_on) {
#if M2HLE_PADS
    if (!pads_on) return;
    pad_refresh();
    ffb_stop_all();
#else
    (void)pads_on;
#endif
}

/* Once a frame: the active profile's controls, loaded when it changes. */
static inline void controls_frame(bool pads_on) {
    if (!g_active_profile || g_active_profile == g_ctl.loaded) return;
    g_ctl.cap_action = -1;
    controls_load(g_active_profile);
    controls_pads_changed(pads_on);
}

/* A key event: true when the window took it (a key being bound), and the game
 * and the shortcuts must not see it. */
static inline bool controls_key_event(const sapp_event *ev) {
    if (ev->type == SAPP_EVENTTYPE_KEY_UP && g_ctl.swallow && (int)ev->key_code == g_ctl.swallow) {
        g_ctl.swallow = 0;
        return true;
    }
    if (g_ctl.cap_action < 0) return false;
    if (ev->type != SAPP_EVENTTYPE_KEY_DOWN) return false;
    int kc = (int)ev->key_code, a = g_ctl.cap_action;
    g_ctl.swallow = kc;
    g_ctl.cap_action = -1;
    if (kc == SAPP_KEYCODE_ESCAPE) return true;
    char name[16];
    if (!input_keycode_name(kc, name)) {
        snprintf(g_ctl.status, sizeof g_ctl.status, "That key cannot be bound.");
        return true;
    }
    controls_edit_begin();
    g_ctl.status[0] = '\0';
    /* A key plays one action: it leaves the one it had. */
    for (int o = 0; o < GAME_INPUT_COUNT; o++)
        for (int s = 0; s < INPUT_KEY_SLOTS; s++)
            if (g_input_keys[o][s] == kc && !(o == a && s == g_ctl.cap_slot)) {
                g_input_keys[o][s] = 0;
                snprintf(g_ctl.status, sizeof g_ctl.status, "%s was %s%s's; it is not any more.", name,
                         o < GAME_INPUT_P2_UP ? "P1 " : o < GAME_INPUT_SERVICE ? "P2 " : "",
                         g_ctl_action_label[o]);
            }
    g_input_keys[a][g_ctl.cap_slot] = kc;
    controls_save();
    return true;
}

/* ---- The window ---------------------------------------------------------- */

/* An action's row label, with what a driving game makes of it. */
static inline void controls_action_label(int a, char *buf, size_t cap) {
    const game_profile_t *p = g_active_profile;
    const char *what = NULL;
    if (p && (p->quirks.io_5649 || p->quirks.model1_io) && p->input.bits[a]) {
        uint32_t bit = p->input.bits[a];
        if (bit == p->quirks.wheel.accel)           what = "accelerator";
        else if (bit == p->quirks.wheel.brake)      what = "brake";
        else if (bit == p->quirks.wheel.shift_up)   what = "shift up";
        else if (bit == p->quirks.wheel.shift_down) what = "shift down";
        else if (bit == p->quirks.wheel.left)       what = "steer left";
        else if (bit == p->quirks.wheel.right)      what = "steer right";
    }
    if (what) snprintf(buf, cap, "%s (%s)", g_ctl_action_label[a], what);
    else      snprintf(buf, cap, "%s", g_ctl_action_label[a]);
}

static inline void controls_keys_rows(int first, int last) {
    char label[48], name[16], id[32];
    for (int a = first; a <= last; a++) {
        igTableNextRow();
        igTableSetColumnIndex(0);
        controls_action_label(a, label, sizeof label);
        bool unused = g_active_profile && !g_active_profile->input.bits[a];
        if (unused) igTextDisabled("%s", label); else igTextUnformatted(label);
        for (int s = 0; s < INPUT_KEY_SLOTS; s++) {
            igTableSetColumnIndex(1 + s);
            const char *shown = "-";
            if (g_ctl.cap_action == a && g_ctl.cap_slot == s) shown = "press a key...";
            else if (g_input_keys[a][s] && input_keycode_name(g_input_keys[a][s], name)) {
                for (char *c = name; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
                shown = name;
            }
            snprintf(id, sizeof id, "%s##k%d_%d", shown, a, s);
            if (igButtonEx(id, (ImVec2){ -1.0f, 0.0f })) { g_ctl.cap_action = a; g_ctl.cap_slot = s; g_ctl.status[0] = '\0'; }
            if (igIsItemClickedEx(ImGuiMouseButton_Right) && g_input_keys[a][s]) {
                controls_edit_begin();
                g_input_keys[a][s] = 0;
                controls_save();
            }
        }
    }
}

static inline void controls_tab_keyboard(void) {
    igTextWrapped("Click a key, then press the one you want (Esc cancels). Right-click clears it.");
    if (!igBeginTableEx("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV, (ImVec2){ 0, 0 }, 0.0f)) return;
    igTableSetupColumnEx("Action", ImGuiTableColumnFlags_WidthStretch, 1.4f, 0);
    igTableSetupColumnEx("Key", ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
    igTableSetupColumnEx("Second key", ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
    igTableHeadersRow();
    igTableNextRow(); igTableSetColumnIndex(0); igSeparatorText("Player 1");
    controls_keys_rows(GAME_INPUT_P1_UP, GAME_INPUT_P1_COIN);
    igTableNextRow(); igTableSetColumnIndex(0); igSeparatorText("Player 2");
    controls_keys_rows(GAME_INPUT_P2_UP, GAME_INPUT_P2_COIN);
    igTableNextRow(); igTableSetColumnIndex(0); igSeparatorText("Cabinet");
    controls_keys_rows(GAME_INPUT_SERVICE, GAME_INPUT_TEST);
    igEndTable();
}

#if M2HLE_PADS
/* What a pad button or wheel button can press: one player-1 action, the
 * cabinet's two, or the usual button combos. */
static const uint32_t g_ctl_pad_choices[] = {
    0, ACT(GAME_INPUT_P1_B1), ACT(GAME_INPUT_P1_B2), ACT(GAME_INPUT_P1_B3), ACT(GAME_INPUT_P1_B4),
    ACT(GAME_INPUT_P1_START), ACT(GAME_INPUT_P1_COIN),
    ACT(GAME_INPUT_P1_UP), ACT(GAME_INPUT_P1_DOWN), ACT(GAME_INPUT_P1_LEFT), ACT(GAME_INPUT_P1_RIGHT),
    ACT(GAME_INPUT_SERVICE), ACT(GAME_INPUT_TEST),
    ACT(GAME_INPUT_P1_B1) | ACT(GAME_INPUT_P1_B2), ACT(GAME_INPUT_P1_B1) | ACT(GAME_INPUT_P1_B3),
    ACT(GAME_INPUT_P1_B2) | ACT(GAME_INPUT_P1_B3), ACT(GAME_INPUT_P1_B1) | ACT(GAME_INPUT_P1_B2) | ACT(GAME_INPUT_P1_B3),
    ACT(GAME_INPUT_P1_B3) | ACT(GAME_INPUT_P1_B4),
};
#define CTL_PAD_CHOICES (int)(sizeof g_ctl_pad_choices / sizeof g_ctl_pad_choices[0])

/* acts in words: "Button 1 + Button 2", "none". */
static inline const char *controls_acts_text(uint32_t acts, char *buf, size_t cap) {
    if (!acts) { snprintf(buf, cap, "none"); return buf; }
    size_t n = 0;
    buf[0] = '\0';
    for (int a = 0; a < GAME_INPUT_COUNT && n < cap; a++)
        if (acts & ACT(a)) {
            char label[48];
            controls_action_label(a, label, sizeof label);
            n += (size_t)snprintf(buf + n, cap - n, "%s%s", n ? " + " : "", label);
        }
    return buf;
}

/* The action picker of one pad or wheel button; true when it changed *acts. */
static inline bool controls_acts_combo(const char *id, uint32_t *acts) {
    char cur[96], item[96];
    bool changed = false;
    igSetNextItemWidth(-1.0f);
    if (!igBeginCombo(id, controls_acts_text(*acts, cur, sizeof cur), 0)) return false;
    for (int c = 0; c < CTL_PAD_CHOICES; c++) {
        if (igSelectableEx(controls_acts_text(g_ctl_pad_choices[c], item, sizeof item),
                           *acts == g_ctl_pad_choices[c], 0, (ImVec2){ 0, 0 })) {
            controls_edit_begin();
            *acts = g_ctl_pad_choices[c];
            changed = true;
        }
    }
    igEndCombo();
    return changed;
}

/* The first gamepad and the first wheel that play here, for the live rows. */
static inline SDL_Gamepad *controls_first_pad(void) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    SDL_Gamepad *pad = NULL;
    for (int i = 0; ids && i < count && !pad; i++) if (pad_plays_here(i)) pad = SDL_GetGamepadFromID(ids[i]);
    SDL_free(ids);
    return pad;
}

static inline SDL_Joystick *controls_first_wheel(void) {
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    SDL_Joystick *joy = NULL;
    for (int i = 0, w = 0; ids && i < count && !joy; i++)
        if (wheel_is(ids[i]) && pad_plays_here(w++)) joy = SDL_GetJoystickFromID(ids[i]);
    SDL_free(ids);
    return joy;
}

static inline void controls_devices(bool pads_on) {
    static const char *const label[PAD_DEVICE_COUNT] = {
        "Keyboard + any gamepad or wheel", "Keyboard only", "Keyboard + gamepad", "Keyboard + racing wheel",
    };
    igSetNextItemWidth(260.0f);
    if (igBeginCombo("Devices", label[g_pad_device], 0)) {
        for (int d = 0; d < PAD_DEVICE_COUNT; d++)
            if (igSelectableEx(label[d], g_pad_device == d, 0, (ImVec2){ 0, 0 }) && g_pad_device != d) {
                controls_edit_begin();
                g_pad_device = d;
                controls_pads_changed(pads_on);
                controls_save();
            }
        igEndCombo();
    }
    if (!pads_on) { igTextDisabled("No gamepads or wheels this run (--no-pads, capture mode, or SDL failed)."); return; }
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    if (!count) igTextDisabled("No gamepad or wheel is connected.");
    for (int i = 0; ids && i < count; i++) {
        bool pad = SDL_IsGamepad(ids[i]), wheel = !pad && wheel_is(ids[i]);
        const char *name = pad ? SDL_GetGamepadNameForID(ids[i]) : SDL_GetJoystickNameForID(ids[i]);
        bool plays = (pad || wheel) && pad_device_takes(wheel);
        igBulletText("%s: %s%s", pad ? "Gamepad" : wheel ? "Wheel" : "Joystick", name ? name : "?",
                     plays ? "" : (pad || wheel) ? " (not playing)" : " (not a wheel: see the Wheel tab)");
    }
    SDL_free(ids);
}

static inline void controls_tab_gamepad(bool pads_on) {
    igTextWrapped("The d-pad and the left stick are player 1's directions; a driving game's wheel is "
                  "the stick, R2 the accelerator and L2 the brake. A held button lights its row.");
    SDL_Gamepad *pad = pads_on ? controls_first_pad() : NULL;
    if (!igBeginTableEx("padbinds", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV, (ImVec2){ 0, 0 }, 0.0f)) return;
    igTableSetupColumnEx("Button", ImGuiTableColumnFlags_WidthFixed, 90.0f, 0);
    igTableSetupColumnEx("Presses", ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
    igTableHeadersRow();
    char id[32];
    for (int b = 0; b < PAD_BIND_COUNT; b++) {
        igTableNextRow();
        if (pad && SDL_GetGamepadButton(pad, g_pad_binds[b].button))
            igTableSetBgColor(ImGuiTableBgTarget_RowBg1, 0x6000C000u, -1);
        igTableSetColumnIndex(0);
        igTextUnformatted(g_pad_binds[b].name);
        igTableSetColumnIndex(1);
        snprintf(id, sizeof id, "##pad%d", b);
        if (controls_acts_combo(id, &g_pad_binds[b].acts)) { controls_pads_changed(pads_on); controls_save(); }
    }
    igEndTable();
}

static inline void controls_tab_wheel(bool pads_on) {
    igTextWrapped("A racing wheel SDL has no gamepad mapping for. Turn the wheel and press the pedals "
                  "to see which axis is which; a held button lights its row.");
    SDL_Joystick *joy = pads_on ? controls_first_wheel() : NULL;
    int naxes = joy ? SDL_GetNumJoystickAxes(joy) : 0, nbut = joy ? SDL_GetNumJoystickButtons(joy) : 0;
    if (joy) igText("Wheel: %s (%d axes, %d buttons)", SDL_GetJoystickName(joy), naxes, nbut);
    else     igTextDisabled("No wheel is connected.");
    int *ax[3] = { &g_wheel_opt.steer, &g_wheel_opt.accel, &g_wheel_opt.brake };
    static const char *const ax_name[3] = { "Wheel axis", "Accelerator axis", "Brake axis" };
    for (int k = 0; k < 3; k++) {
        int v = *ax[k];
        igSetNextItemWidth(120.0f);
        if (igInputIntEx(ax_name[k], &v, 1, 1, 0) && v >= 0 && v < 16) {
            controls_edit_begin();
            *ax[k] = v;
            controls_pads_changed(pads_on);
            controls_save();
        }
        if (joy && *ax[k] < naxes) {
            igSameLine();
            float f = (SDL_GetJoystickAxis(joy, *ax[k]) + 32768.0f) / 65535.0f;
            igProgressBar(f, (ImVec2){ 160.0f, 0 }, "");
        }
    }
    int rows = nbut > 12 ? nbut : 12;
    if (rows > WHEEL_BUTTONS) rows = WHEEL_BUTTONS;
    if (!igBeginTableEx("wheelbinds", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY,
                        (ImVec2){ 0, 260.0f }, 0.0f)) return;
    igTableSetupColumnEx("Button", ImGuiTableColumnFlags_WidthFixed, 90.0f, 0);
    igTableSetupColumnEx("Presses", ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
    igTableHeadersRow();
    char id[32];
    for (int b = 0; b < rows; b++) {
        igTableNextRow();
        if (joy && b < nbut && SDL_GetJoystickButton(joy, b))
            igTableSetBgColor(ImGuiTableBgTarget_RowBg1, 0x6000C000u, -1);
        igTableSetColumnIndex(0);
        igText("Button %d", b);
        igTableSetColumnIndex(1);
        snprintf(id, sizeof id, "##wheel%d", b);
        if (controls_acts_combo(id, &g_wheel_opt.button[b])) { controls_pads_changed(pads_on); controls_save(); }
    }
    igEndTable();
}
#endif

static inline void controls_window_draw(bool *open, bool pads_on) {
    if (!*open) { g_ctl.cap_action = -1; return; }
    igSetNextWindowSize((ImVec2){ 520.0f, 620.0f }, ImGuiCond_FirstUseEver);
    if (!igBegin("Controls", open, 0)) { igEnd(); return; }
    const game_profile_t *p = g_ctl.loaded;
    char path[640];
    igText("Controls for %s", p ? p->display_name : "(no profile)");
    if (controls_path(p, path, sizeof path)) igTextDisabled("Saved in %s", path);
    if (g_ctl.cli_n) igTextColored((ImVec4){ 1, 0.8f, 0.3f, 1 }, "Command-line pad options apply; a change here drops them.");
#if M2HLE_PADS
    controls_devices(pads_on);
#else
    (void)pads_on;
    igTextDisabled("This build has the keyboard alone.");
#endif
    if (g_ctl.status[0]) igTextColored((ImVec4){ 1, 0.8f, 0.3f, 1 }, "%s", g_ctl.status);
    igSeparator();
    if (igBeginTabBar("ctltabs", 0)) {
        if (igBeginTabItem("Keyboard", NULL, 0)) { controls_tab_keyboard(); igEndTabItem(); }
#if M2HLE_PADS
        if (igBeginTabItem("Gamepad", NULL, 0)) { controls_tab_gamepad(pads_on); igEndTabItem(); }
        if (igBeginTabItem("Wheel", NULL, 0))   { controls_tab_wheel(pads_on); igEndTabItem(); }
#endif
        igEndTabBar();
    }
    igSeparator();
    if (igButton("Reset to defaults") && p) {
        g_ctl.cli_n = 0;
        if (controls_path(p, path, sizeof path)) remove(path);
        controls_load(p);
        controls_pads_changed(pads_on);
        snprintf(g_ctl.status, sizeof g_ctl.status, "Back to the built-in controls.");
    }
    igEnd();
}

#endif /* CONTROLS_WINDOW_H */
