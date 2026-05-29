/*
 * trigger4p AtomS3 status display — bootstrap.
 * Brings up HAL → state model → UI splash → BLE → main loop (UI tick + button poll).
 */

#ifdef M5ATOMS3

#include "m5atoms3_hal.h"
#include "m5atoms3_gfx.h"
#include "trigger_state.h"
#include "trigger_ble.h"
#include "trigger_proto.h"
#include "trigger_ui.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

static const char *TAG = "MAIN";

/* Two-output wiring matches the ESPHome bridge: passenger = APK ch2,
 * driver = APK ch3. The single AtomS3 button drives "both" together. */
#define BTN_POLL_MS         20
#define BTN_LONG_MS         800   /* press >= this = long-press (dim cycle)   */
#define BTN_DBL_GAP_MS      350   /* second click within this = double-click  */
#define DIM_STEP            51    /* 51..255 in 5 steps, same as ESPHome       */
#define DIM_MAX             255

typedef enum { BTN_EV_NONE, BTN_EV_SHORT, BTN_EV_DOUBLE, BTN_EV_LONG } btn_event_t;

/* Optimistic local model (the box does not report dim, and channel notifies
 * may lag); the UI still reflects real channel state from notifications. */
static bool    s_both_on    = false;
static bool    s_both_blink = false;
static uint8_t s_dim_level  = DIM_MAX;

/* Edge-detect + classify the single button into SHORT / DOUBLE / LONG. */
static btn_event_t poll_button_event(void) {
    static bool     was_down       = false;
    static int64_t  down_us        = 0;
    static bool     pending_short  = false;
    static int64_t  first_up_us    = 0;

    int64_t now = esp_timer_get_time();
    bool down = atoms3_hal_button_pressed();

    if (down && !was_down) {                 /* press edge */
        was_down = true;
        down_us  = now;
    } else if (!down && was_down) {           /* release edge */
        was_down = false;
        int64_t held_ms = (now - down_us) / 1000;
        if (held_ms >= BTN_LONG_MS) {
            pending_short = false;
            return BTN_EV_LONG;
        }
        if (pending_short && (now - first_up_us) / 1000 <= BTN_DBL_GAP_MS) {
            pending_short = false;
            return BTN_EV_DOUBLE;
        }
        pending_short = true;                 /* maybe a double — wait for gap */
        first_up_us   = now;
    } else if (pending_short &&
               (now - first_up_us) / 1000 > BTN_DBL_GAP_MS) {
        pending_short = false;                /* gap elapsed — it was a single */
        return BTN_EV_SHORT;
    }
    return BTN_EV_NONE;
}

static void apply_both(bool on, bool blink) {
    /* ON path resets dim to full first, like the ESPHome single-click. */
    if (on && blink) {
        trigger_ble_send_action(TRG_CH2, TRG_ACT_BLINK);
        trigger_ble_send_action(TRG_CH3, TRG_ACT_BLINK);
    } else if (on) {
        trigger_ble_send_action(TRG_CH2, TRG_ACT_ON);
        trigger_ble_send_action(TRG_CH3, TRG_ACT_ON);
    } else {
        trigger_ble_send_action(TRG_CH2, TRG_ACT_OFF);
        trigger_ble_send_action(TRG_CH3, TRG_ACT_OFF);
    }
}

static void handle_button_event(btn_event_t ev) {
    if (ev == BTN_EV_NONE || !trigger_ble_is_linked()) return;
    switch (ev) {
    case BTN_EV_SHORT:
        s_both_on = !s_both_on;
        if (s_both_on) {
            s_dim_level = DIM_MAX;
            trigger_ble_send_dim(DIM_MAX);
        }
        s_both_blink = false;
        apply_both(s_both_on, false);
        break;
    case BTN_EV_DOUBLE:
        s_both_blink = !s_both_blink;
        s_both_on = true;             /* blink only shows on a lit channel */
        apply_both(true, s_both_blink);
        break;
    case BTN_EV_LONG: {
        int next = (int)s_dim_level + DIM_STEP;
        if (next > DIM_MAX) next = DIM_STEP;
        s_dim_level = (uint8_t)next;
        trigger_ble_send_dim(s_dim_level);
        break;
    }
    default:
        break;
    }
}

static void boot_splash(void) {
    /* Quick text-based splash so we know the panel is alive before BLE init. */
    atoms3_gfx_clear(M5_COLOR_BLACK);
    atoms3_gfx_print_centered(28, "TRIGGER",  M5_COLOR_WHITE, 3);
    atoms3_gfx_print_centered(70, "4 PLUS",   M5_COLOR_GREEN, 3);
    atoms3_gfx_print_centered(110, "ATOMS3 BOOT", M5_COLOR_GREY, 1);
    vTaskDelay(pdMS_TO_TICKS(1200));
    atoms3_gfx_clear(M5_COLOR_BLACK);
}

void app_main(void) {
    ESP_LOGI(TAG, "trigger4p AtomS3 — boot");

    /* NVS is required by the BT stack for PHY calibration. */
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs = nvs_flash_init();
    }
    if (nvs != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(nvs));
    }

    /* Bring up display + button. */
    if (atoms3_hal_init() != ESP_OK) {
        ESP_LOGE(TAG, "HAL init failed — bailing");
        return;
    }
    boot_splash();

    /* Logical state model (mutex + caches). */
    trigger_state_init();
    trigger_state_set_link(TRG_LINK_BOOT);

    /* UI ready (will paint on first tick). */
    trigger_ui_init();
    trigger_ui_tick();

    /* Start BLE — the GATT client task drives discovery, connect, notify, keepalive. */
    if (trigger_ble_init() != ESP_OK) {
        ESP_LOGE(TAG, "BLE init failed");
    }

    /* Main loop: poll button at 20 ms (for click classification), repaint UI
     * roughly every 150 ms. */
    int ticks_since_paint = 0;
    while (1) {
        handle_button_event(poll_button_event());
        if (++ticks_since_paint >= (150 / BTN_POLL_MS)) {
            trigger_ui_tick();
            ticks_since_paint = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

#endif /* M5ATOMS3 */
