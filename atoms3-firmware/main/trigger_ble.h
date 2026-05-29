/*
 * BLE GATT client for TRIGGER 4 Plus.
 *
 * Responsibilities:
 *   - Init the bluedroid stack as a GATT central.
 *   - Scan for advertisements from the configured TRIGGER unit (by name
 *     "Trigger 4 Plus" or by pinned MAC if TRIGGER_PIN_MAC is set).
 *   - Open a GATTC connection, discover service 0xFFF0, and capture the
 *     handles for write characteristic 0xFFF6 and notify char 0xFFF7.
 *   - Subscribe to notifications on 0xFFF7 (required — see README.md).
 *   - Run a 200 ms keepalive task while linked.
 *   - Push parsed notifications into trigger_state_*.
 *   - Re-discover and reconnect on drop.
 *
 * Local control: trigger_ble_send_action / trigger_ble_send_dim build a frame
 * via trigger_proto and write it to 0xFFF6 (WRITE_NO_RSP). Both are no-ops
 * when the link is down, and are safe to call from the main loop.
 */

#ifndef TRIGGER_BLE_H
#define TRIGGER_BLE_H

#include "esp_err.h"
#include "trigger_proto.h"

esp_err_t trigger_ble_init(void);

/* True once connected + write handle resolved (commands will actually go out). */
bool trigger_ble_is_linked(void);

/* Send one channel action (ON/OFF/BLINK/STEADY). No-op if not linked. */
void trigger_ble_send_action(trg_channel_t ch, trg_action_t act);

/* Set the shared dim register. ui_level 0..255, 255 = brightest. No-op if
 * not linked. Also records the level in trigger_state for the UI. */
void trigger_ble_send_dim(uint8_t ui_level);

#endif /* TRIGGER_BLE_H */
