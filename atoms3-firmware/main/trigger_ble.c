/*
 * BLE GATT client for TRIGGER 4 Plus.
 *
 * Implementation pattern follows ESP-IDF gattc examples:
 *   - one application profile registered against `esp_ble_gattc_register_app`
 *   - one GAP scan to find the device (name match or pinned MAC)
 *   - on found: stop scan, esp_ble_gattc_open
 *   - on connect: search service 0xFFF0, get char handles for 0xFFF6 and 0xFFF7
 *   - register-for-notify on 0xFFF7, write CCCD = 0x0001 to enable
 *   - start keepalive task; push 8-byte keepalive every 200 ms via WRITE_NO_RSP
 *   - on notify event: parse the 5-byte state frame and feed trigger_state
 *   - on disconnect: stop keepalive, mark LINK_LOST, scan again
 *
 * The TRIGGER 4 Plus characteristic constraints make this firmware different
 * from the average BLE example:
 *   - 0xFFF6 only supports Write Without Response (opcode 0x52). Using
 *     write-with-response (opcode 0x12) gets ATT error 0x03 (see README.md).
 *   - 0xFFF7 NOTIFY must be subscribed for the whole session — leaving it
 *     idle while spamming writes causes cross-channel flicker.
 *   - Keepalive cadence is ~150-300 ms; we use 200 ms.
 */

#ifdef M5ATOMS3

#include "trigger_ble.h"
#include "trigger_proto.h"
#include "trigger_state.h"
#include "secrets.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatt_common_api.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include <string.h>

static const char *TAG = "TRG_BLE";

/* Service / characteristic UUIDs. The TRIGGER box advertises 16-bit UUIDs in
 * the 0xFFFx range under service 0xFFF0. */
#define TRG_SVC_UUID_16     0xFFF0
#define TRG_CHAR_WRITE_16   0xFFF6
#define TRG_CHAR_NOTIFY_16  0xFFF7

#define TRG_DEVICE_NAME     "Trigger 4 Plus"

#define TRG_PROFILE_APP_ID  0
#define TRG_INVALID_HANDLE  0

#define TRG_KEEPALIVE_MS    200

/* ───────────────────────────── module state ────────────────────────────── */

typedef struct {
    esp_gatt_if_t gattc_if;
    uint16_t      conn_id;
    uint16_t      service_start_handle;
    uint16_t      service_end_handle;
    uint16_t      write_handle;
    uint16_t      notify_handle;
    esp_bd_addr_t remote_bda;
    bool          remote_bda_known;
    bool          connected;
    bool          notify_subscribed;
    bool          scanning;
} trg_ble_t;

static trg_ble_t      s_ble = { .gattc_if = ESP_GATT_IF_NONE };
static TaskHandle_t   s_keepalive_task = NULL;

/* ──────────────────────────── helper functions ─────────────────────────── */

static void start_scan(void) {
    /* Active scan, 30 ms window inside 50 ms interval — fast enough to find
     * the box quickly without saturating the radio. duration=0 = forever. */
    static esp_ble_scan_params_t scan_params = {
        .scan_type          = BLE_SCAN_TYPE_ACTIVE,
        .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
        .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_interval      = 0x50,  /* 0x50 * 0.625 ms = 50 ms */
        .scan_window        = 0x30,  /* 0x30 * 0.625 ms = 30 ms */
        .scan_duplicate     = BLE_SCAN_DUPLICATE_DISABLE,
    };
    esp_err_t r = esp_ble_gap_set_scan_params(&scan_params);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "set_scan_params: %s", esp_err_to_name(r));
    }
    /* The actual esp_ble_gap_start_scan(0) call happens from the
     * SET_SCAN_PARAMS_COMPLETE GAP event below. */
    s_ble.scanning = true;
    trigger_state_set_link(TRG_LINK_DISCOVERY);
    ESP_LOGI(TAG, "Scanning for '%s'...", TRG_DEVICE_NAME);
}

static bool addr_matches_pinned(const esp_bd_addr_t bda) {
    /* MAC pinning omitted in MVP — name-based discovery is enough for a
     * single-unit pairing. If you have multiple TRIGGER 4 Plus units in
     * BLE range and need to pin one, replace this stub with a memcmp
     * against a 6-byte literal MAC. */
    (void)bda;
    return false;
}

/* Return true if the scan result's name matches TRG_DEVICE_NAME. The TRIGGER
 * box advertises its complete local name in the SCAN RESPONSE, not the primary
 * ADV packet, so a manual walk of `ble_adv[0..adv_data_len]` misses it. The
 * stack's esp_ble_resolve_adv_data walks the concatenated adv + scan-response
 * TLV buffer and finds the name wherever it lives. */
static bool adv_name_matches(uint8_t *ble_adv) {
    if (ble_adv == NULL) return false;
    uint8_t name_len = 0;
    uint8_t *name = esp_ble_resolve_adv_data(ble_adv, ESP_BLE_AD_TYPE_NAME_CMPL, &name_len);
    if (name == NULL || name_len == 0) {
        name = esp_ble_resolve_adv_data(ble_adv, ESP_BLE_AD_TYPE_NAME_SHORT, &name_len);
    }
    if (name == NULL || name_len == 0) return false;
    size_t want = strlen(TRG_DEVICE_NAME);
    return name_len == want && memcmp(name, TRG_DEVICE_NAME, want) == 0;
}

/* Send one keepalive frame. Called from the keepalive task at 200 ms cadence
 * while connected. WRITE_NO_RSP is required — the box rejects WRITE_RSP. */
static void send_keepalive(void) {
    if (!s_ble.connected || s_ble.write_handle == TRG_INVALID_HANDLE) return;
    uint8_t frame[TRG_FRAME_LEN];
    trigger_proto_build_keepalive(frame, TRIGGER_DEVICE_ID, TRIGGER_PASSWORD);
    esp_err_t r = esp_ble_gattc_write_char(s_ble.gattc_if, s_ble.conn_id,
                                           s_ble.write_handle,
                                           sizeof(frame), frame,
                                           ESP_GATT_WRITE_TYPE_NO_RSP,
                                           ESP_GATT_AUTH_REQ_NONE);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "keepalive write failed: %s", esp_err_to_name(r));
    }
}

static void keepalive_task(void *arg) {
    (void)arg;
    while (1) {
        send_keepalive();
        vTaskDelay(pdMS_TO_TICKS(TRG_KEEPALIVE_MS));
    }
}

static void start_keepalive_task_once(void) {
    if (s_keepalive_task != NULL) return;
    BaseType_t ok = xTaskCreate(keepalive_task, "trg_keepalive", 3072,
                                NULL, 5, &s_keepalive_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "keepalive task create failed");
        s_keepalive_task = NULL;
    } else {
        ESP_LOGI(TAG, "keepalive task started (%d ms)", TRG_KEEPALIVE_MS);
    }
}

/* After a successful connect we walk the GATT database to find service 0xFFF0
 * and the two characteristic handles inside it. */
static void resolve_service(void) {
    esp_gattc_service_elem_t svc_elem;
    uint16_t count = 1;
    esp_bt_uuid_t svc_uuid = {
        .len = ESP_UUID_LEN_16,
        .uuid = { .uuid16 = TRG_SVC_UUID_16 },
    };
    esp_gatt_status_t st = esp_ble_gattc_get_service(s_ble.gattc_if, s_ble.conn_id,
                                                     &svc_uuid, &svc_elem, &count, 0);
    if (st != ESP_GATT_OK || count == 0) {
        ESP_LOGE(TAG, "service 0xFFF0 not found (status=%d)", st);
        return;
    }
    s_ble.service_start_handle = svc_elem.start_handle;
    s_ble.service_end_handle   = svc_elem.end_handle;
    ESP_LOGI(TAG, "service 0xFFF0 handles %d..%d",
             s_ble.service_start_handle, s_ble.service_end_handle);

    /* Look up write char 0xFFF6 + notify char 0xFFF7 inside the service range. */
    esp_gattc_char_elem_t  char_elem;
    uint16_t               char_count;

    esp_bt_uuid_t write_uuid  = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = TRG_CHAR_WRITE_16 } };
    esp_bt_uuid_t notify_uuid = { .len = ESP_UUID_LEN_16, .uuid = { .uuid16 = TRG_CHAR_NOTIFY_16 } };

    char_count = 1;
    st = esp_ble_gattc_get_char_by_uuid(s_ble.gattc_if, s_ble.conn_id,
                                        s_ble.service_start_handle,
                                        s_ble.service_end_handle,
                                        write_uuid, &char_elem, &char_count);
    if (st == ESP_GATT_OK && char_count > 0) {
        s_ble.write_handle = char_elem.char_handle;
        ESP_LOGI(TAG, "write char 0xFFF6 handle = 0x%04X", s_ble.write_handle);
    } else {
        ESP_LOGE(TAG, "0xFFF6 not found (status=%d)", st);
    }

    char_count = 1;
    st = esp_ble_gattc_get_char_by_uuid(s_ble.gattc_if, s_ble.conn_id,
                                        s_ble.service_start_handle,
                                        s_ble.service_end_handle,
                                        notify_uuid, &char_elem, &char_count);
    if (st == ESP_GATT_OK && char_count > 0) {
        s_ble.notify_handle = char_elem.char_handle;
        ESP_LOGI(TAG, "notify char 0xFFF7 handle = 0x%04X", s_ble.notify_handle);

        /* Subscribe — register-for-notify + write CCCD descriptor = 0x0001. */
        esp_err_t r = esp_ble_gattc_register_for_notify(s_ble.gattc_if,
                                                        s_ble.remote_bda,
                                                        s_ble.notify_handle);
        if (r != ESP_OK) {
            ESP_LOGE(TAG, "register_for_notify failed: %s", esp_err_to_name(r));
        }
    } else {
        ESP_LOGE(TAG, "0xFFF7 not found (status=%d)", st);
    }
}

/* When register-for-notify completes successfully, the stack still needs the
 * CCCD descriptor written to actually start receiving notifications. */
static void enable_cccd(void) {
    if (s_ble.notify_handle == TRG_INVALID_HANDLE) return;
    esp_gattc_descr_elem_t descr_elem;
    uint16_t count = 1;
    esp_bt_uuid_t cccd_uuid = {
        .len = ESP_UUID_LEN_16,
        .uuid = { .uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG },
    };
    esp_gatt_status_t st = esp_ble_gattc_get_descr_by_char_handle(
        s_ble.gattc_if, s_ble.conn_id, s_ble.notify_handle,
        cccd_uuid, &descr_elem, &count);
    if (st != ESP_GATT_OK || count == 0) {
        ESP_LOGE(TAG, "CCCD descriptor lookup failed (status=%d)", st);
        return;
    }
    uint8_t v[2] = { 0x01, 0x00 }; /* notifications */
    esp_err_t r = esp_ble_gattc_write_char_descr(s_ble.gattc_if, s_ble.conn_id,
                                                 descr_elem.handle,
                                                 sizeof(v), v,
                                                 ESP_GATT_WRITE_TYPE_RSP,
                                                 ESP_GATT_AUTH_REQ_NONE);
    if (r == ESP_OK) {
        s_ble.notify_subscribed = true;
        trigger_state_set_link(TRG_LINK_LINKED);
        start_keepalive_task_once();
        ESP_LOGI(TAG, "CCCD enabled — link is up");
    } else {
        ESP_LOGE(TAG, "CCCD write failed: %s", esp_err_to_name(r));
    }
}

/* ─────────────────────────────── GAP events ────────────────────────────── */

static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *p) {
    switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if (p->scan_param_cmpl.status == ESP_BT_STATUS_SUCCESS) {
            /* IDF 5.4 renamed start_scan -> start_scanning. duration=0 = forever. */
            esp_ble_gap_start_scanning(0);
        }
        break;

    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
        if (!s_ble.scanning) break;
        struct ble_scan_result_evt_param *sr = &p->scan_rst;
        if (sr->search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;

        bool match = adv_name_matches(sr->ble_adv);
        if (!match) match = addr_matches_pinned(sr->bda);
        if (!match) break;

        ESP_LOGI(TAG, "Found '%s' at %02X:%02X:%02X:%02X:%02X:%02X — connecting",
                 TRG_DEVICE_NAME,
                 sr->bda[0], sr->bda[1], sr->bda[2],
                 sr->bda[3], sr->bda[4], sr->bda[5]);

        memcpy(s_ble.remote_bda, sr->bda, 6);
        s_ble.remote_bda_known = true;
        s_ble.scanning = false;
        esp_ble_gap_stop_scanning();
        trigger_state_set_link(TRG_LINK_CONNECTING);
        esp_ble_gattc_open(s_ble.gattc_if, sr->bda, sr->ble_addr_type, true);
        break;
    }

    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        break;

    default:
        break;
    }
}

/* ─────────────────────────────── GATTC events ──────────────────────────── */

static void gattc_profile_event_handler(esp_gattc_cb_event_t event,
                                        esp_gatt_if_t gattc_if,
                                        esp_ble_gattc_cb_param_t *p) {
    switch (event) {
    case ESP_GATTC_REG_EVT:
        s_ble.gattc_if = gattc_if;
        start_scan();
        break;

    case ESP_GATTC_OPEN_EVT:
        if (p->open.status != ESP_GATT_OK) {
            ESP_LOGW(TAG, "open failed (%d) — rescanning", p->open.status);
            s_ble.connected = false;
            start_scan();
            break;
        }
        s_ble.conn_id   = p->open.conn_id;
        s_ble.connected = true;
        ESP_LOGI(TAG, "GATTC open OK, conn_id=%d, MTU=%d", p->open.conn_id, p->open.mtu);
        /* Kick off service discovery for our service UUID specifically. */
        esp_ble_gattc_search_service(gattc_if, p->open.conn_id,
                                     &(esp_bt_uuid_t){ .len = ESP_UUID_LEN_16,
                                                       .uuid = { .uuid16 = TRG_SVC_UUID_16 } });
        break;

    case ESP_GATTC_SEARCH_CMPL_EVT:
        resolve_service();
        break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
        if (p->reg_for_notify.status == ESP_GATT_OK) {
            enable_cccd();
        } else {
            ESP_LOGE(TAG, "reg_for_notify failed (%d)", p->reg_for_notify.status);
        }
        break;

    case ESP_GATTC_NOTIFY_EVT: {
        if (p->notify.handle != s_ble.notify_handle) break;
        trg_state_t st;
        trigger_proto_parse_state(p->notify.value, p->notify.value_len, &st);
        trigger_state_set_channels(&st);
        ESP_LOGI(TAG, "notify state=0x%02X valid=%d ch1=%d ch2=%d ch3=%d ch4=%d "
                       "blink1=%d blink2=%d",
                 st.raw_state, st.valid,
                 st.ch1_on, st.ch2_on, st.ch3_on, st.ch4_on,
                 st.ch1_blink, st.ch2_blink);
        break;
    }

    case ESP_GATTC_DISCONNECT_EVT:
        ESP_LOGW(TAG, "disconnected (reason=0x%X) — rescanning", p->disconnect.reason);
        if (s_keepalive_task != NULL) {
            vTaskDelete(s_keepalive_task);
            s_keepalive_task = NULL;
        }
        s_ble.connected         = false;
        s_ble.notify_subscribed = false;
        s_ble.write_handle      = TRG_INVALID_HANDLE;
        s_ble.notify_handle     = TRG_INVALID_HANDLE;
        trigger_state_inc_drops();
        trigger_state_set_link(TRG_LINK_LOST);
        start_scan();
        break;

    default:
        break;
    }
}

static void gattc_event_handler(esp_gattc_cb_event_t event,
                                esp_gatt_if_t gattc_if,
                                esp_ble_gattc_cb_param_t *p) {
    /* Single-profile firmware — route everything to the one handler. */
    gattc_profile_event_handler(event, gattc_if, p);
}

/* ──────────────────────────── command send path ────────────────────────── */

/* Write one TRG_FRAME_LEN frame to the 0xFFF6 write characteristic. The box
 * only accepts Write Without Response (0x52); WRITE_RSP gets ATT error 0x03. */
static void write_frame(const uint8_t *frame) {
    if (!s_ble.connected || s_ble.write_handle == TRG_INVALID_HANDLE) return;
    esp_err_t r = esp_ble_gattc_write_char(s_ble.gattc_if, s_ble.conn_id,
                                           s_ble.write_handle,
                                           TRG_FRAME_LEN, (uint8_t *)frame,
                                           ESP_GATT_WRITE_TYPE_NO_RSP,
                                           ESP_GATT_AUTH_REQ_NONE);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "write_frame failed: %s", esp_err_to_name(r));
    }
}

bool trigger_ble_is_linked(void) {
    return s_ble.connected && s_ble.notify_subscribed;
}

void trigger_ble_send_action(trg_channel_t ch, trg_action_t act) {
    if (!trigger_ble_is_linked()) return;
    uint8_t frame[TRG_FRAME_LEN];
    trigger_proto_build_action(frame, TRIGGER_DEVICE_ID, TRIGGER_PASSWORD, ch, act);
    /* Mirror the phone app / ESPHome bridge: send the payload twice with a
     * short gap. A single WRITE_NO_RSP is occasionally dropped by the box's
     * GATT server under keepalive contention. */
    write_frame(frame);
    vTaskDelay(pdMS_TO_TICKS(30));
    write_frame(frame);
}

void trigger_ble_send_dim(uint8_t ui_level) {
    if (!trigger_ble_is_linked()) return;
    uint8_t frame[TRG_FRAME_LEN];
    trigger_proto_build_dim(frame, TRIGGER_DEVICE_ID, TRIGGER_PASSWORD, ui_level);
    write_frame(frame);
    vTaskDelay(pdMS_TO_TICKS(30));
    write_frame(frame);
    trigger_state_set_dim(ui_level);
}

/* ─────────────────────────────────── init ──────────────────────────────── */

esp_err_t trigger_ble_init(void) {
    esp_err_t r;

    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    r = esp_bt_controller_init(&bt_cfg);
    if (r != ESP_OK) { ESP_LOGE(TAG, "controller_init: %s", esp_err_to_name(r)); return r; }

    r = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (r != ESP_OK) { ESP_LOGE(TAG, "controller_enable: %s", esp_err_to_name(r)); return r; }

    r = esp_bluedroid_init();
    if (r != ESP_OK) { ESP_LOGE(TAG, "bluedroid_init: %s", esp_err_to_name(r)); return r; }

    r = esp_bluedroid_enable();
    if (r != ESP_OK) { ESP_LOGE(TAG, "bluedroid_enable: %s", esp_err_to_name(r)); return r; }

    r = esp_ble_gap_register_callback(gap_event_handler);
    if (r != ESP_OK) { ESP_LOGE(TAG, "gap reg: %s", esp_err_to_name(r)); return r; }

    r = esp_ble_gattc_register_callback(gattc_event_handler);
    if (r != ESP_OK) { ESP_LOGE(TAG, "gattc reg cb: %s", esp_err_to_name(r)); return r; }

    r = esp_ble_gattc_app_register(TRG_PROFILE_APP_ID);
    if (r != ESP_OK) { ESP_LOGE(TAG, "gattc app reg: %s", esp_err_to_name(r)); return r; }

    /* Slightly larger MTU than 23 lets the box reply with whatever frame size
     * its GATT server prefers. The TRIGGER firmware uses 23 in advertising
     * but accepts MTU exchanges; not strictly required for command frames. */
    r = esp_ble_gatt_set_local_mtu(64);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "set_local_mtu: %s (continuing)", esp_err_to_name(r));
    }

    s_ble.write_handle  = TRG_INVALID_HANDLE;
    s_ble.notify_handle = TRG_INVALID_HANDLE;
    return ESP_OK;
}

#endif /* M5ATOMS3 */
