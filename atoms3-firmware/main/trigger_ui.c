/*
 * trigger4p AtomS3 "Ditch LEDs" UI — M5GFX, diff-based partial repaints.
 *
 * Layout (128x128 panel, rotation=2), shared visual frame with the DJI remote:
 *
 *   +----------------------+
 *   | DITCH         [link] |  TOP BAND: title + 🔗 icon when linked (red dot on
 *   |                      |           loss; clear while scanning).
 *   |        60%           |  MAIN: when linked, yellow fill rises bottom->top
 *   |######################|        with the commanded dim level ("OFF!" red when
 *   |######################|        off, fill flashes while blink mode is on).
 *   +----------------------+        While not linked, a blinking 📡 dish shows.
 *   |    D          P      |  BOTTOM BAND: driver / passenger letters driven by
 *   +----------------------+               the box's real FFF7 feedback.
 *
 * Truthfulness: the % fill is the level we COMMAND (the box never echoes dim).
 * The D/P dots reflect the box's reported channel state only (ch3=driver,
 * ch2=passenger), never the optimistic command.
 */

#ifdef M5ATOMS3

#include "trigger_ui.h"
#include "trigger_state.h"
#include "m5atoms3_hal.h"
#include "m5atoms3_gfx.h"
#include "icons_png.h"

#include <stdio.h>
#include <string.h>

#define SCREEN_W        128
#define SCREEN_H        128
#define TOP_BAND_H      20
#define BOT_BAND_H      30
#define MAIN_TOP        (TOP_BAND_H + 1)
#define MAIN_BOT        (SCREEN_H - BOT_BAND_H - 1)
#define MAIN_H          (MAIN_BOT - MAIN_TOP)

#define CONN_DOT_R      5
#define CONN_DOT_CX     (SCREEN_W - 11)
#define CONN_DOT_CY     (TOP_BAND_H / 2)

#define ICON_LINK_W     16            /* icon_link_png is 16x16 */
#define ICON_SAT_W      64            /* icon_sat_png  is 64x64 */

#define BAND_BG         M5_COLOR_DARKGREY
#define BLINK_PERIOD_TICKS 3   /* UI ticks (~150 ms each) per blink half-cycle */

typedef struct {
    bool     chrome_drawn;
    int      last_link;
    int      last_pct;        /* -1 = unknown */
    bool     last_on;
    bool     last_blink;
    bool     last_blink_visible;
    bool     last_d_on;
    bool     last_p_on;
    bool     last_fb_valid;
} ui_cache_t;

static ui_cache_t s_cache;
static uint32_t   s_tick;

/* Top-right connectivity indicator: linked shows the 🔗 link icon; lost shows
 * a red dot; while scanning/connecting the main area carries the blinking 📡
 * dish, so the top cell stays clear. */
static void draw_top_indicator(int link) {
    atoms3_gfx_fill_rect(SCREEN_W - 20, 0, 20, TOP_BAND_H, BAND_BG);
    if (link == TRG_LINK_LINKED) {
        atoms3_gfx_draw_png(SCREEN_W - ICON_LINK_W - 2, (TOP_BAND_H - ICON_LINK_W) / 2,
                            icon_link_png, icon_link_png_len);
    } else if (link == TRG_LINK_LOST) {
        atoms3_gfx_fill_circle(CONN_DOT_CX, CONN_DOT_CY, CONN_DOT_R, M5_COLOR_RED);
    }
}

/* Map commanded dim (0..255, 0xFFFF=unknown) to 0..100 %. */
static int dim_to_pct(uint16_t dim) {
    if (dim == 0xFFFF) return 100;          /* on but level not yet sent */
    if (dim > 255) dim = 255;
    return (int)((dim * 100 + 127) / 255);
}

static void draw_top_band(int link) {
    atoms3_gfx_fill_rect(0, 0, SCREEN_W, TOP_BAND_H, BAND_BG);
    atoms3_gfx_print(4, 2, "DITCH", M5_COLOR_WHITE, 1);   /* small label tier */
    draw_top_indicator(link);
}

static void clear_main(void) {
    atoms3_gfx_fill_rect(0, MAIN_TOP, SCREEN_W, MAIN_H + 1, M5_COLOR_BLACK);
}

/* Draw the main hero: scanning dish, OFF, or the yellow level fill. */
static void draw_main(int link, bool on, bool blink, int pct, bool blink_visible) {
    if (link != TRG_LINK_LINKED) {
        /* Blink the dish by only touching its 64x64 box (no full-main clear, so
         * the panel doesn't flash): draw on the visible phase, paint the box
         * black on the off phase. The full main was cleared on link change. */
        int ix = (SCREEN_W - ICON_SAT_W) / 2;
        int iy = MAIN_TOP + (MAIN_H - ICON_SAT_W) / 2;
        if (blink_visible) {
            atoms3_gfx_draw_png(ix, iy, icon_sat_png, icon_sat_png_len);
        } else {
            atoms3_gfx_fill_rect(ix, iy, ICON_SAT_W, ICON_SAT_W, M5_COLOR_BLACK);
        }
        return;
    }

    clear_main();

    if (!on) {
        atoms3_gfx_print_centered(MAIN_TOP + MAIN_H / 2 - 16, "OFF!", M5_COLOR_RED, 3);
        return;
    }

    /* Yellow fill from the bottom up. When blinking, the fill disappears on
     * the off phase so the panel visibly flashes (we can dim a blink). */
    if (!blink || blink_visible) {
        int fill_h = (MAIN_H * pct) / 100;
        if (fill_h < 0) fill_h = 0;
        if (fill_h > MAIN_H) fill_h = MAIN_H;
        if (fill_h > 0) {
            atoms3_gfx_fill_rect(0, MAIN_BOT - fill_h, SCREEN_W, fill_h, M5_COLOR_YELLOW);
        }
    }

    char pctbuf[8];
    snprintf(pctbuf, sizeof(pctbuf), "%d%%", pct);
    /* percent sits near the top of the main area; black on yellow if the fill
     * has reached it, white on black otherwise. */
    int text_y = MAIN_TOP + 6;
    int fill_top = MAIN_BOT - (MAIN_H * pct) / 100;
    bool over_fill = (!blink || blink_visible) && (text_y + 20 >= fill_top);
    uint16_t pct_color = over_fill ? M5_COLOR_BLACK : M5_COLOR_WHITE;
    atoms3_gfx_print_centered(text_y, pctbuf, pct_color, 3);

    if (blink) {
        atoms3_gfx_print_centered(MAIN_BOT - 16, "BLINK",
                                  blink_visible ? M5_COLOR_BLACK : M5_COLOR_YELLOW, 1);
    }
}

/* Bottom band: D (driver=ch3) and P (passenger=ch2) reflect REAL feedback.
 * Each half is a colored swatch with a black letter so it's readable:
 *   channel ON     -> yellow cell  (black D/P pops)
 *   channel OFF    -> light grey cell (black D/P still readable)
 *   no feedback yet-> dark band cell, dim grey letter (looks inactive)
 */
#define DP_OFF_BG    0x9CD3   /* light grey cell when channel is off */

static void draw_bottom_band(bool fb_valid, bool d_on, bool p_on) {
    int y0 = SCREEN_H - BOT_BAND_H;
    int halfw = SCREEN_W / 2;
    int h = atoms3_gfx_tier_pixel_height(2);
    int ty = y0 + (BOT_BAND_H - h) / 2 + 1;   /* nudged down 1px */

    struct { const char *lbl; bool on; int x0; int w; int cx; } cells[2] = {
        { "D", d_on, 0,          halfw,            SCREEN_W / 4 },
        { "P", p_on, halfw + 1,  SCREEN_W - halfw - 1, (SCREEN_W * 3) / 4 },
    };
    for (int i = 0; i < 2; i++) {
        uint16_t bg = !fb_valid ? BAND_BG : (cells[i].on ? M5_COLOR_YELLOW : DP_OFF_BG);
        uint16_t fg = !fb_valid ? M5_COLOR_GREY : M5_COLOR_BLACK;
        atoms3_gfx_fill_rect(cells[i].x0, y0, cells[i].w, BOT_BAND_H, bg);
        atoms3_gfx_print_centered_at(cells[i].cx, ty, cells[i].lbl, fg, 2);
    }
    /* 1px black seam between the two halves */
    atoms3_gfx_fill_rect(halfw, y0, 1, BOT_BAND_H, M5_COLOR_BLACK);
}

void trigger_ui_init(void) {
    memset(&s_cache, 0, sizeof(s_cache));
    s_cache.chrome_drawn = false;
    s_cache.last_link    = -1;
    s_cache.last_pct     = -2;
    s_tick               = 0;
}

void trigger_ui_tick(void) {
    s_tick++;

    int  link = (int)trigger_state_get_link();
    bool cmd_on = false, cmd_blink = false;
    trigger_state_get_command(&cmd_on, &cmd_blink);
    int  pct = dim_to_pct(trigger_state_get_dim());

    trg_state_t fb;
    trigger_state_get_channels(&fb);
    bool fb_valid = fb.valid;
    bool d_on = fb.valid && fb.ch3_on;   /* driver    = APK ch3 */
    bool p_on = fb.valid && fb.ch2_on;   /* passenger = APK ch2 */

    bool blink_visible = ((s_tick / BLINK_PERIOD_TICKS) & 1u) == 0u;
    bool not_linked = (link != TRG_LINK_LINKED);

    if (!s_cache.chrome_drawn) {
        atoms3_gfx_clear(M5_COLOR_BLACK);
        draw_top_band(link);
        s_cache.chrome_drawn   = true;
        s_cache.last_link      = link;
        s_cache.last_pct       = -2;        /* force main + band repaint below */
        s_cache.last_d_on      = !d_on;
        s_cache.last_p_on      = !p_on;
        s_cache.last_fb_valid  = !fb_valid;
    }

    if (link != s_cache.last_link) {
        draw_top_indicator(link);
        clear_main();                     /* wipe stale hero before the new state */
        s_cache.last_link = link;
        s_cache.last_pct  = -2;           /* link change repaints the hero */
    }

    bool main_changed =
        pct != s_cache.last_pct ||
        cmd_on != s_cache.last_on ||
        cmd_blink != s_cache.last_blink ||
        (cmd_on && cmd_blink && blink_visible != s_cache.last_blink_visible) ||
        (not_linked && blink_visible != s_cache.last_blink_visible);

    if (main_changed) {
        draw_main(link, cmd_on, cmd_blink, pct, blink_visible);
        s_cache.last_pct           = pct;
        s_cache.last_on            = cmd_on;
        s_cache.last_blink         = cmd_blink;
        s_cache.last_blink_visible = blink_visible;
    }

    if (d_on != s_cache.last_d_on || p_on != s_cache.last_p_on ||
        fb_valid != s_cache.last_fb_valid) {
        draw_bottom_band(fb_valid, d_on, p_on);
        s_cache.last_d_on     = d_on;
        s_cache.last_p_on     = p_on;
        s_cache.last_fb_valid = fb_valid;
    }
}

#endif /* M5ATOMS3 */
