/*
 * link_check_pad.h — the question of core/link_check.h (Pinboard #1449) for a
 * frontend with only a pad and the PS3 menus' canvas: libretro. A box over the
 * paused game: up and down pick, cross (RetroPad B) or Start answers. The
 * virtual TOSlink cable is offered here and nowhere else, as on the desktop.
 *
 * Once a frame, on the thread that runs the board: link_check_pad_frame with
 * the pad in PS3UI_PAD_* bits. While it says so, the board does not run and
 * the game gets none of the pad; after an answer the game still gets none of
 * it until the answering button is let go, so its press does not reach the
 * game. link_check_pad_draw records the box on the canvas over the game.
 */
#ifndef UI_LINK_CHECK_PAD_H
#define UI_LINK_CHECK_PAD_H

#include "link_check.h"
#include "ps3ui_app.h"

typedef enum {
    LINK_PAD_NONE,      /* nothing asked: the game runs and has the pad */
    LINK_PAD_ASKING,    /* the box is up: the board waits, the game gets no pad */
    LINK_PAD_RELEASE,   /* answered, buttons still down: the board runs, no pad */
} link_pad_state_t;

typedef struct {
    link_pad_state_t state;
    int sel;
    bool test;          /* the profile has a Test button: three answers */
    uint32_t pad_was;
} link_pad_t;

static link_pad_t g_link_pad;

#define LINK_PAD_ANSWER (PS3UI_PAD_CROSS | PS3UI_PAD_START)

static inline int link_pad__rows(const link_pad_t *lp, link_answer_t rows[3]) {
    int n = 0;
    if (lp->test) rows[n++] = LINK_ANSWER_TEST_MENU;
    rows[n++] = LINK_ANSWER_CONTINUE;
    rows[n++] = LINK_ANSWER_CABLE;
    return n;
}

/* A set was loaded: nothing on screen. */
static inline void link_check_pad_reset(void) {
    g_link_pad.state = LINK_PAD_NONE;
}

static inline link_pad_state_t link_check_pad_frame(uint32_t pad, uint32_t board_frame) {
    link_pad_t *lp = &g_link_pad;
    uint32_t hit = pad & ~lp->pad_was;
    lp->pad_was = pad;
    if (lp->state == LINK_PAD_RELEASE && !(pad & LINK_PAD_ANSWER)) lp->state = LINK_PAD_NONE;
    if (lp->state == LINK_PAD_NONE) {
        if (!link_check_due()) return LINK_PAD_NONE;
        lp->state = LINK_PAD_ASKING;
        lp->sel   = 0;
        lp->test  = link_check_has_test(g_active_profile);
        LOG_WARN("link: this cabinet is the link's MASTER and no other cabinet is linked; asking");
        return lp->state;   /* a button down now was not pressed at the box */
    }
    if (lp->state != LINK_PAD_ASKING) return lp->state;
    link_answer_t rows[3];
    int n = link_pad__rows(lp, rows);
    if ((hit & PS3UI_PAD_UP) && lp->sel > 0) lp->sel--;
    if ((hit & PS3UI_PAD_DOWN) && lp->sel < n - 1) lp->sel++;
    if (hit & LINK_PAD_ANSWER) {
        link_check_answer(rows[lp->sel], board_frame);
        lp->state = LINK_PAD_RELEASE;
    }
    return lp->state;
}

static inline bool link_check_pad_up(void) { return g_link_pad.state == LINK_PAD_ASKING; }

/* The box, in the canvas's 1920x1080 layout. True when it drew. */
static inline bool link_check_pad_draw(ps3ui_canvas_t *cv) {
    const link_pad_t *lp = &g_link_pad;
    if (lp->state != LINK_PAD_ASKING) return false;
    float black[3] = { 0.0f, 0.0f, 0.0f };
    ps3ui_fill_px(cv, 0, 0, cv->w, cv->h, black, 0.6f, PS3UI_BLEND_NORMAL);
    const ps3ui_box_t win = { 260.0f, 200.0f, 1660.0f, 880.0f };
    ps3ui_fill_rect(cv, win, 0x0B1A33, 0.95f, PS3UI_BLEND_NORMAL);
    ps3ui_fill_rect(cv, (ps3ui_box_t){ win.x0, win.y0, win.x1, win.y0 + 4.0f }, 0x4DD2FF, 1.0f, PS3UI_BLEND_NORMAL);

    ps3ui_text_style_t title = ps3ui_style_text(40.0f);
    title.rgb = 0xFFCC4D;
    ps3ui_text_left(cv, &title, win.x0 + 50.0f, win.y0 + 30.0f, LINK_CHECK_TITLE, 1.0f);
    ps3ui_text_style_t body = ps3ui_style_text(30.0f);
    ps3ui_text_box(cv, &body, (ps3ui_box_t){ win.x0 + 50.0f, win.y0 + 110.0f, win.x1 - 50.0f, win.y0 + 400.0f },
                   LINK_CHECK_TEXT, 1.0f, 0);

    static const char *const label[] = { "Back to the test menu", "Continue anyway",
                                         "Insert a virtual TOSlink cable" };
    link_answer_t rows[3];
    int n = link_pad__rows(lp, rows);
    float y = win.y1 - 70.0f - 56.0f * (float)n;
    ps3ui_text_style_t row = ps3ui_style_text(34.0f);
    for (int i = 0; i < n; i++, y += 56.0f) {
        bool on = i == lp->sel;
        if (on) ps3ui_fill_rect(cv, (ps3ui_box_t){ win.x0 + 40.0f, y - 6.0f, win.x0 + 760.0f, y + 50.0f },
                                0x4DD2FF, 0.25f, PS3UI_BLEND_NORMAL);
        row.rgb = on ? 0xFFFFFF : 0xA0B4C8;
        ps3ui_text_left(cv, &row, win.x0 + 70.0f, y, label[rows[i]], 1.0f);
    }
    ps3ui_text_style_t hint = ps3ui_style_text(26.0f);
    hint.rgb = 0xA0B4C8;
    if (rows[lp->sel] == LINK_ANSWER_CABLE)
        ps3ui_text_box(cv, &hint, (ps3ui_box_t){ win.x0 + 800.0f, y - 56.0f * (float)n, win.x1 - 50.0f, y },
                       "Wires this comm board's output to its own input: a link of one cabinet, "
                       "so the MASTER finds itself. Until another game is loaded.", 1.0f, 0);
    ps3ui_text_left(cv, &hint, win.x0 + 70.0f, win.y1 - 56.0f, "Up / Down: choose     B or Start: answer", 1.0f);
    return true;
}

#endif /* UI_LINK_CHECK_PAD_H */
