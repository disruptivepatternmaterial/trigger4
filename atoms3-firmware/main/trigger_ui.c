/*
 * trigger4p AtomS3 status display — M5GFX-based, diff-based partial repaints
 * so the screen does not flicker on every tick.
 *
 * Layout (128x128 panel, rotation=2):
 *
 *   +----------------------+
 *   |TRG          [conn]   |   tier 1, tag + colored conn dot
 *   |                      |
 *   |     LINKED           |   tier 3, hero state word
 *   |                      |
 *   | C1[ON ] C2[OFF]      |   tier 1, 2x2 grid of channel pills
 *   | C3[OFF] C4[BLK]      |
 *   |                      |
 *   | DIM 204    DROPS 0   |   tier 1, diagnostic line
 *   +----------------------+
 *
 * Color rules:
 *   ON      → green
 *   OFF     → grey
 *   BLINK   → yellow
 *   LINK_LINKED → green hero, LOST/CONNECTING → yellow, BOOT/SCAN → grey
 */

#ifdef M5ATOMS3

#include "trigger_ui.h"
#include "trigger_state.h"
#include "m5atoms3_hal.h"
#include "m5atoms3_gfx.h"

#include <stdio.h>
#include <string.h>

#define MARGIN_X        4
#define LABEL_Y         2
#define CONN_DOT_X      (128 - 12)
#define CONN_DOT_Y      4
#define CONN_DOT_R      4
#define HERO_Y          26
#define PILL_ROW_Y0     70
#define PILL_ROW_Y1     90
#define PILL_W          56
#define PILL_H          16
#define DIAG_Y          110

typedef struct {
    bool      drawn_chrome;
    int       last_link;
    char      hero[16];
    uint16_t  hero_color;
    bool      ch_on[4];
    bool      ch_blink[4];   /* blink only meaningful for ch1/ch2 */
    uint16_t  dim;
    uint32_t  drops;
} ui_cache_t;

static ui_cache_t s_cache;

static const char *link_word(int s) {
    switch (s) {
    case TRG_LINK_BOOT:       return "BOOT";
    case TRG_LINK_DISCOVERY:  return "SCANNING";
    case TRG_LINK_CONNECTING: return "CONNECTING";
    case TRG_LINK_LINKED:     return "LINKED";
    case TRG_LINK_LOST:       return "LOST";
    default:                  return "?";
    }
}

static uint16_t link_color(int s) {
    switch (s) {
    case TRG_LINK_LINKED:     return M5_COLOR_GREEN;
    case TRG_LINK_CONNECTING: return M5_COLOR_YELLOW;
    case TRG_LINK_LOST:       return M5_COLOR_ORANGE;
    case TRG_LINK_DISCOVERY:  return M5_COLOR_YELLOW;
    case TRG_LINK_BOOT:
    default:                  return M5_COLOR_GREY;
    }
}

static uint16_t conn_dot_color(int s) {
    switch (s) {
    case TRG_LINK_LINKED:     return M5_COLOR_GREEN;
    case TRG_LINK_CONNECTING: return M5_COLOR_YELLOW;
    case TRG_LINK_DISCOVERY:  return M5_COLOR_YELLOW;
    case TRG_LINK_LOST:       return M5_COLOR_ORANGE;
    default:                  return M5_COLOR_DARKGREY;
    }
}

static void draw_pill(int x, int y, const char *ch_label, bool on, bool blink) {
    /* Pill background = state color, label = "C1 ON" / "C1 OFF" / "C1 BLK". */
    uint16_t bg = blink ? M5_COLOR_YELLOW : (on ? M5_COLOR_GREEN : M5_COLOR_DARKGREY);
    uint16_t fg = (bg == M5_COLOR_GREEN || bg == M5_COLOR_DARKGREY) ? M5_COLOR_WHITE
                                                                    : M5_COLOR_BLACK;
    atoms3_gfx_fill_rect(x, y, PILL_W, PILL_H, bg);

    char buf[10];
    const char *state = blink ? "BLK" : (on ? "ON" : "OFF");
    snprintf(buf, sizeof(buf), "%s %s", ch_label, state);

    /* Center the text within the pill. We use the small tier-1 font so it
     * fits 56 px wide at scale 1. */
    atoms3_gfx_print(x + 4, y + 1, buf, fg, 1);
}

void trigger_ui_init(void) {
    memset(&s_cache, 0, sizeof(s_cache));
    s_cache.drawn_chrome = false;
    s_cache.last_link    = -1;
    s_cache.dim          = 0xFFFE; /* force first paint */
    s_cache.drops        = 0xFFFFFFFFu;
}

void trigger_ui_tick(void) {
    if (!s_cache.drawn_chrome) {
        atoms3_gfx_clear(M5_COLOR_BLACK);
        atoms3_gfx_print(MARGIN_X, LABEL_Y, "TRG", M5_COLOR_WHITE, 1);
        s_cache.drawn_chrome = true;
        s_cache.last_link    = -1; /* force everything else to repaint */
        s_cache.hero[0]      = '\0';
        s_cache.hero_color   = 0;
        for (int i = 0; i < 4; i++) { s_cache.ch_on[i] = false; s_cache.ch_blink[i] = false; }
        s_cache.dim          = 0xFFFE;
        s_cache.drops        = 0xFFFFFFFFu;
    }

    int link = (int)trigger_state_get_link();
    if (link != s_cache.last_link) {
        /* Conn dot. */
        atoms3_gfx_erase_rect(CONN_DOT_X - CONN_DOT_R - 1,
                              CONN_DOT_Y - CONN_DOT_R - 1,
                              CONN_DOT_R * 2 + 3,
                              CONN_DOT_R * 2 + 3);
        atoms3_gfx_fill_circle(CONN_DOT_X, CONN_DOT_Y + CONN_DOT_R,
                               CONN_DOT_R, conn_dot_color(link));
        s_cache.last_link = link;

        /* Hero state word. */
        const char *word = link_word(link);
        uint16_t    color = link_color(link);
        if (strcmp(word, s_cache.hero) != 0 || color != s_cache.hero_color) {
            atoms3_gfx_erase_rect(0, HERO_Y, atoms3_gfx_width(),
                                  atoms3_gfx_tier_pixel_height(3) + 2);
            atoms3_gfx_print_centered(HERO_Y, word, color, 3);
            strncpy(s_cache.hero, word, sizeof(s_cache.hero) - 1);
            s_cache.hero[sizeof(s_cache.hero) - 1] = '\0';
            s_cache.hero_color = color;
        }
    }

    /* Channel pills (2x2). Pull current channel state. */
    trg_state_t chs;
    trigger_state_get_channels(&chs);
    bool on[4]    = { chs.ch1_on,    chs.ch2_on,    chs.ch3_on, chs.ch4_on };
    bool blink[4] = { chs.ch1_blink, chs.ch2_blink, false,      false      };
    /* If we have not received any notifications yet (chs.valid=false), show
     * all channels as OFF/grey rather than UNKNOWN — keeps the layout stable
     * during the boot → linked transition. */

    /* Pill positions: left column x = MARGIN_X, right column x = pill+gap. */
    int x_left  = MARGIN_X;
    int x_right = MARGIN_X + PILL_W + 4;
    int positions_x[4] = { x_left, x_right, x_left,    x_right };
    int positions_y[4] = { PILL_ROW_Y0, PILL_ROW_Y0, PILL_ROW_Y1, PILL_ROW_Y1 };
    static const char *labels[4] = { "C1", "C2", "C3", "C4" };

    for (int i = 0; i < 4; i++) {
        if (on[i] != s_cache.ch_on[i] || blink[i] != s_cache.ch_blink[i] || !s_cache.drawn_chrome) {
            draw_pill(positions_x[i], positions_y[i], labels[i], on[i], blink[i]);
            s_cache.ch_on[i]    = on[i];
            s_cache.ch_blink[i] = blink[i];
        }
    }

    /* Diagnostic line: dim + drops. */
    uint16_t dim   = trigger_state_get_dim();
    uint32_t drops = trigger_state_get_drops();
    if (dim != s_cache.dim || drops != s_cache.drops) {
        atoms3_gfx_erase_rect(0, DIAG_Y, atoms3_gfx_width(),
                              atoms3_gfx_tier_pixel_height(1) + 2);
        char buf[40];
        if (dim == 0xFFFF) {
            snprintf(buf, sizeof(buf), "DIM --   DROPS %lu",
                     (unsigned long)drops);
        } else {
            snprintf(buf, sizeof(buf), "DIM %3u  DROPS %lu",
                     (unsigned)(dim & 0xFF), (unsigned long)drops);
        }
        atoms3_gfx_print(MARGIN_X, DIAG_Y, buf, M5_COLOR_GREY, 1);
        s_cache.dim   = dim;
        s_cache.drops = drops;
    }
}

#endif /* M5ATOMS3 */
