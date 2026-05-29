/*
 * Slim AtomS3 HAL for trigger4p status display. Display init goes to the
 * M5GFX shim; we only own the screen-button GPIO here.
 */

#ifdef M5ATOMS3

#include "m5atoms3_hal.h"
#include "m5atoms3_gfx.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "ATOMS3_HAL";

esp_err_t atoms3_hal_init(void) {
    /* Display first — M5GFX configures backlight, SPI, panel. */
    int rc = atoms3_gfx_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "M5GFX init failed (rc=%d)", rc);
        return ESP_FAIL;
    }
    /* Single screen-button on G41, active-low, internal pull-up. */
    gpio_config_t io = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << M5_BTN_A_PIN),
        .pull_down_en = 0,
        .pull_up_en = 1,
    };
    gpio_config(&io);
    ESP_LOGI(TAG, "AtomS3 HAL initialized — button on G%d", M5_BTN_A_PIN);
    return ESP_OK;
}

bool atoms3_hal_button_pressed(void) {
    return gpio_get_level(M5_BTN_A_PIN) == 0;
}

#endif /* M5ATOMS3 */
