/*
 * AtomS3 graphics shim — wraps M5GFX (LovyanGFX-based) with a C-callable API.
 * Mirrors the DJI-remote project's shim. The trigger4p firmware uses the same
 * tier-based fonts so the two AtomS3 builds look visually consistent.
 */

#ifdef M5ATOMS3

#include <M5GFX.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "esp_log.h"

extern "C" {
#include "m5atoms3_gfx.h"
}

static const char *TAG = "ATOMS3_GFX";

static M5GFX gfx;

extern "C" int atoms3_gfx_init(void) {
    if (!gfx.init()) {
        ESP_LOGE(TAG, "M5GFX init() returned false");
        return -1;
    }
    /* Rotation 2 = 180°; matches DJI-remote orientation. */
    gfx.setRotation(2);
    gfx.setBrightness(255);
    gfx.fillScreen(TFT_BLACK);
    gfx.setTextColor(TFT_WHITE, TFT_BLACK);
    gfx.setTextDatum(top_left);
    ESP_LOGI(TAG, "M5GFX init OK — %dx%d, rotation=2",
             (int)gfx.width(), (int)gfx.height());
    return 0;
}

extern "C" void atoms3_gfx_set_brightness(uint8_t b) { gfx.setBrightness(b); }
extern "C" int  atoms3_gfx_width(void)               { return (int)gfx.width(); }
extern "C" int  atoms3_gfx_height(void)              { return (int)gfx.height(); }

extern "C" void atoms3_gfx_clear(uint16_t color)                              { gfx.fillScreen(color); }
extern "C" void atoms3_gfx_fill_rect(int x,int y,int w,int h,uint16_t c)      { gfx.fillRect(x,y,w,h,c); }
extern "C" void atoms3_gfx_fill_circle(int cx,int cy,int r,uint16_t c)        { gfx.fillCircle(cx,cy,r,c); }
extern "C" void atoms3_gfx_erase_rect(int x,int y,int w,int h)                { gfx.fillRect(x,y,w,h,TFT_BLACK); }
extern "C" void atoms3_gfx_draw_xbitmap(int x,int y,int w,int h,const uint8_t *bm,uint16_t c) {
    if (bm) gfx.drawXBitmap(x,y,bm,w,h,c);
}

static void atoms3_gfx_pick_font_for_tier(int tier) {
    switch (tier) {
        case 3: gfx.setFont(&fonts::Font4); gfx.setTextSize(1); break;
        case 2: gfx.setFont(&fonts::Font2); gfx.setTextSize(2); break;
        case 1:
        default: gfx.setFont(&fonts::Font2); gfx.setTextSize(1); break;
    }
}

extern "C" void atoms3_gfx_print(int x,int y,const char *t,uint16_t c,int tier) {
    if (!t) return;
    atoms3_gfx_pick_font_for_tier(tier);
    gfx.setTextColor(c, TFT_BLACK);
    gfx.setTextDatum(top_left);
    gfx.drawString(t, x, y);
}

extern "C" void atoms3_gfx_print_centered(int y,const char *t,uint16_t c,int tier) {
    if (!t) return;
    atoms3_gfx_pick_font_for_tier(tier);
    gfx.setTextColor(c, TFT_BLACK);
    gfx.setTextDatum(top_center);
    gfx.drawString(t, gfx.width()/2, y);
}

extern "C" int atoms3_gfx_tier_pixel_height(int tier) {
    atoms3_gfx_pick_font_for_tier(tier);
    return (int)gfx.fontHeight();
}

#endif /* M5ATOMS3 */
