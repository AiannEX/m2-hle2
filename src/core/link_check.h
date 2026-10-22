/*
 * link_check.h — a linked game set to MASTER with no other cabinet on the link
 * (Pinboard #1449).
 *
 * Daytona USA, Virtual-On, Power Sled and the other linked sets keep their
 * cabinet's role on the link in the test menu (Daytona: GAME ASSIGNMENTS, LINK
 * ID). A board set to MASTER starts its comm board as the master and waits for
 * the other cabinets; alone, it waits for nobody. Daytona shows NETWORK
 * CHECKING, then CANCELLED, reboots and does it again, forever, and only the
 * test menu gets out of it. The board counts each start of its comm board as a
 * master alone (m2comm_lone_masters, board/m2comm.h); the host pauses the game
 * at the next one and asks what to do:
 *
 *   LINK_ANSWER_TEST_MENU  back to the test menu, to set the cabinet SINGLE:
 *                          Test is held for 20 frames (Daytona answers it in
 *                          NETWORK CHECKING too)
 *   LINK_ANSWER_CONTINUE   go on as it is; not asked again for this set
 *   LINK_ANSWER_CABLE      plug in a virtual TOSlink cable: the comm board's
 *                          output into its own input, a ring of one cabinet
 *                          (m2comm_set_cable). Offered only here.
 *
 * Not asked of a board that is not the player's: a netplay session's or a
 * replay's (backup RAM detached), which boots blank, so SINGLE, anyway; a
 * session's own link takes the cable's place. A headless run never asks:
 * get_status's link_pace reports the count and the cable, and the bridge's
 * link_check answers. The desktop, the web page and libretro ask
 * (ui/link_check_prompt.h, web_link_check, ui/link_check_pad.h); SDL3 only
 * says it, once (link_check_tell).
 *
 * UI thread. The count is the emu thread's, read without the mutex: a new
 * start shows up a frame later at worst.
 */
#ifndef CORE_LINK_CHECK_H
#define CORE_LINK_CHECK_H

#include <stdbool.h>
#include <stdint.h>

#include "backup_ram.h"
#include "game_profile.h"
#include "input.h"
#include "log.h"
#include "m2comm.h"

#define LINK_CHECK_TEST_FRAMES 20u   /* board frames Test is held for */

typedef enum { LINK_ANSWER_TEST_MENU, LINK_ANSWER_CONTINUE, LINK_ANSWER_CABLE } link_answer_t;

typedef struct {
    uint32_t handled;     /* lone-master starts answered */
    bool     quiet;       /* LINK_ANSWER_CONTINUE: not asked again for this set */
    bool     test_held;   /* Test is held, until board frame test_until */
    uint32_t test_until;
} link_check_t;

static link_check_t g_link_check;

/* A set was loaded: nothing asked yet, the cable out. */
static inline void link_check_reset(void) {
    if (g_link_check.test_held) input_action_up(GAME_INPUT_TEST);
    g_link_check.handled   = m2comm_lone_masters();
    g_link_check.quiet     = false;
    g_link_check.test_held = false;
    m2comm_set_cable(false);
}

/* Whether the profile has a Test button to go back to the test menu with. */
static inline bool link_check_has_test(const game_profile_t *p) {
    return p && p->input.bits[GAME_INPUT_TEST] != 0;
}

/* True while a start of the board as a master alone waits for an answer. */
static inline bool link_check_due(void) {
    uint32_t n = m2comm_lone_masters();
    if (n == g_link_check.handled) return false;
    if (g_link_check.quiet || (g_backup.set[0] && !g_backup.attached)) {
        g_link_check.handled = n;
        return false;
    }
    return true;
}

/* board_frame: the board's frame count now (g_emu_frames). */
static inline void link_check_answer(link_answer_t a, uint32_t board_frame) {
    g_link_check.handled = m2comm_lone_masters();
    switch (a) {
        case LINK_ANSWER_TEST_MENU:
            if (!g_link_check.test_held) input_action_down(GAME_INPUT_TEST);
            g_link_check.test_held  = true;
            g_link_check.test_until = board_frame + LINK_CHECK_TEST_FRAMES;
            break;
        case LINK_ANSWER_CONTINUE: g_link_check.quiet = true; break;
        case LINK_ANSWER_CABLE:    m2comm_set_cable(true);    break;
    }
}

/* A frontend with no way to ask (SDL3): true once per set, at the
 * first lone start, to say so on screen; never asked again for the set. */
static inline bool link_check_tell(void) {
    if (!link_check_due()) return false;
    link_check_answer(LINK_ANSWER_CONTINUE, 0);
    LOG_WARN("link: this cabinet is the link's MASTER and no other cabinet is linked; "
             "set it to SINGLE in the test menu");
    return true;
}

/* The Test bits a frontend that sets the board's held bits itself each frame
 * (libretro) adds to them while Test is held for the answer. */
static inline uint32_t link_check_test_bits(void) {
    return g_link_check.test_held && g_active_profile ? g_active_profile->input.bits[GAME_INPUT_TEST] : 0;
}

/* Once a host frame: lets go of Test once the board has run its frames. Until
 * then Test stays down whatever else lets go of it: on the web the answering
 * key's release reaches the game, and a key up clears an action it never
 * pressed (input_action_up). */
static inline void link_check_frame(uint32_t board_frame) {
    g_input.held |= link_check_test_bits();   /* not a second down: one up lets go */
    if (g_link_check.test_held && (int32_t)(board_frame - g_link_check.test_until) >= 0) {
        g_link_check.test_held = false;
        input_action_up(GAME_INPUT_TEST);
    }
}

/* The question, for every frontend that asks it. */
#define LINK_CHECK_TITLE "No other cabinet on the link"
/* The one line a frontend with no way to ask shows (link_check_tell). */
#define LINK_CHECK_LINE \
    "No other cabinet on the link: this MASTER waits for nobody. Set LINK ID to SINGLE in the test menu."
#define LINK_CHECK_TEXT \
    "This cabinet is set to be the link's MASTER, but no other cabinet is linked to it.\n" \
    "It will wait for one, give up and restart, over and over.\n" \
    "Set it to SINGLE in the test menu's game settings (Daytona USA: GAME ASSIGNMENTS, LINK ID)."

#endif /* CORE_LINK_CHECK_H */
