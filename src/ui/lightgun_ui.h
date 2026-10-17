/*
 * lightgun_ui.h — the desktop's mouse as P1's light gun, and the crosshairs
 * (Pinboard #1034).
 *
 * For a profile with quirks.lightgun (The House of the Dead): the mouse over
 * the game's picture aims P1's gun (lightgun_point), the left button is the
 * trigger (P1 B1) and the right button reloads (P1 B2, which lightgun.h makes
 * an off-screen shot). The mouse off the picture points the gun off it, so a
 * click there reloads too, as on the cabinet. A key that walks P1's gun hands
 * it back to the pad until the mouse moves again. Clicks an ImGui window takes
 * are not shots.
 *
 * The crosshairs are drawn where the board's guns point (lightgun_aim), not
 * where the mouse is: in a session or a replay that is the pad's aim.
 */
#ifndef M2HLE_LIGHTGUN_UI_H
#define M2HLE_LIGHTGUN_UI_H

#include <stdbool.h>

#include "cimgui.h"
#include "sokol_app.h"

#include "game_profile.h"
#include "input.h"
#include "lightgun.h"

typedef struct { int x, y, w, h; } lightgun_ui_rect_t;

static lightgun_ui_rect_t s_lightgun_ui_rect;          /* the game's picture, last frame */
static bool               s_lightgun_ui_down[2];       /* left, right held by the mouse */

static inline bool lightgun_ui_on(void) {
    return g_active_profile && g_active_profile->quirks.lightgun;
}

/* Where the picture went this frame (window_game_rect), framebuffer pixels. */
static inline void lightgun_ui_set_rect(int x, int y, int w, int h) {
    s_lightgun_ui_rect = (lightgun_ui_rect_t){ x, y, w, h };
}

static inline void lightgun_ui_aim_at(float mx, float my) {
    const lightgun_ui_rect_t *r = &s_lightgun_ui_rect;
    if (r->w <= 0 || r->h <= 0) return;
    float fx = (mx - (float)r->x) / (float)r->w, fy = (my - (float)r->y) / (float)r->h;
    bool off = fx < 0.0f || fx > 1.0f || fy < 0.0f || fy > 1.0f;
    lightgun_point(0, true, off, fx, fy);
}

static inline void lightgun_ui_button(int b, bool down) {
    int act = b == 0 ? GAME_INPUT_P1_B1 : GAME_INPUT_P1_B2;
    if (down == s_lightgun_ui_down[b]) return;
    s_lightgun_ui_down[b] = down;
    if (down) input_action_down(act); else input_action_up(act);
}

static inline void lightgun_ui_event(const sapp_event *ev) {
    if (!lightgun_ui_on()) return;
    switch (ev->type) {
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        lightgun_ui_aim_at(ev->mouse_x, ev->mouse_y);
        break;
    case SAPP_EVENTTYPE_MOUSE_LEAVE:
        lightgun_point(0, true, true, 0.5f, 0.5f);
        break;
    case SAPP_EVENTTYPE_MOUSE_DOWN:
    case SAPP_EVENTTYPE_MOUSE_UP: {
        int b = ev->mouse_button == SAPP_MOUSEBUTTON_LEFT  ? 0
              : ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT ? 1 : -1;
        if (b < 0) break;
        bool down = ev->type == SAPP_EVENTTYPE_MOUSE_DOWN;
        if (down && igGetIO()->WantCaptureMouse) break;    /* a click on a window */
        if (down) lightgun_ui_aim_at(ev->mouse_x, ev->mouse_y);
        lightgun_ui_button(b, down);
        break;
    }
    case SAPP_EVENTTYPE_KEY_DOWN: {
        int a = input_keycode_to_action((int)ev->key_code);
        if (a == GAME_INPUT_P1_UP || a == GAME_INPUT_P1_DOWN || a == GAME_INPUT_P1_LEFT || a == GAME_INPUT_P1_RIGHT)
            lightgun_point(0, false, false, 0.5f, 0.5f);
        break;
    }
    case SAPP_EVENTTYPE_UNFOCUSED:
        lightgun_ui_button(0, false);
        lightgun_ui_button(1, false);
        break;
    default: break;
    }
}

/* A crosshair per gun: P1 red; P2 blue, once it has left its rest. */
static inline void lightgun_ui_draw(int ox, int oy, int w, int h) {
    if (!lightgun_ui_on() || w <= 0 || h <= 0) return;
    float dpi = sapp_dpi_scale() > 0 ? sapp_dpi_scale() : 1.0f;
    ImDrawList *dl = igGetBackgroundDrawList();
    float s = (float)h / 384.0f * 10.0f / dpi;     /* the arm, ~10 board pixels */
    for (int p = 0; p < LIGHTGUN_PLAYERS; p++) {
        if (p == 1 && !g_lightgun_pointer[1].on && g_lightgun.x[1] == g_lightgun_range[1].x_rest
                   && g_lightgun.y[1] == g_lightgun_range[1].y_rest) continue;
        float fx, fy;
        lightgun_aim(p, &fx, &fy);
        ImVec2 c = { ((float)ox + fx * (float)w) / dpi, ((float)oy + fy * (float)h) / dpi };
        ImU32 col = p == 0 ? 0xFF2020E0u : 0xFFFF6030u;    /* IM_COL32 order: red, blue */
        ImU32 shade = 0xA0000000u;
        for (int k = 0; k < 2; k++) {
            ImU32 cc = k ? col : shade;
            float t = k ? 2.0f : 4.0f;
            ImDrawList_AddCircleEx(dl, c, s * 0.6f, cc, 0, t);
            ImDrawList_AddLineEx(dl, (ImVec2){ c.x - s, c.y }, (ImVec2){ c.x - s * 0.3f, c.y }, cc, t);
            ImDrawList_AddLineEx(dl, (ImVec2){ c.x + s * 0.3f, c.y }, (ImVec2){ c.x + s, c.y }, cc, t);
            ImDrawList_AddLineEx(dl, (ImVec2){ c.x, c.y - s }, (ImVec2){ c.x, c.y - s * 0.3f }, cc, t);
            ImDrawList_AddLineEx(dl, (ImVec2){ c.x, c.y + s * 0.3f }, (ImVec2){ c.x, c.y + s }, cc, t);
        }
    }
}

#endif /* M2HLE_LIGHTGUN_UI_H */
