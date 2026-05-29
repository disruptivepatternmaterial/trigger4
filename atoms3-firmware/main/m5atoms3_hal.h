/*
 * Slim AtomS3 HAL for the trigger4p status display.
 * No IMU, no buzzer (we don't need motion or audio for this device).
 * Display is delegated to the M5GFX shim (m5atoms3_gfx.cpp).
 */

#ifndef M5ATOMS3_HAL_H
#define M5ATOMS3_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define M5_BTN_A_PIN        41

/* Standard RGB565 colors — M5GFX renders these correctly without a swap. */
#define M5_COLOR_BLACK      0x0000
#define M5_COLOR_WHITE      0xFFFF
#define M5_COLOR_RED        0xF800
#define M5_COLOR_GREEN      0x07E0
#define M5_COLOR_BLUE       0x001F
#define M5_COLOR_YELLOW     0xFFE0
#define M5_COLOR_CYAN       0x07FF
#define M5_COLOR_MAGENTA    0xF81F
#define M5_COLOR_ORANGE     0xFD20
#define M5_COLOR_DARKGREY   0x39C6
#define M5_COLOR_GREY       0x7BEF

esp_err_t atoms3_hal_init(void);

bool      atoms3_hal_button_pressed(void);

#endif /* M5ATOMS3_HAL_H */
