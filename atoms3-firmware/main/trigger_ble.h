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
 * Also drives the relay: the single AtomS3 button maps to action/dim frames
 * sent on the 0xFFF6 write characteristic (see trigger_ble_send_*).
 */

#ifndef TRIGGER_BLE_H
#define TRIGGER_BLE_H

#include <stdbool.h>
#include "esp_err.h"
#include "trigger_proto.h"

esp_err_t trigger_ble_init(void);

/* True once connected AND notifications are subscribed (full link up). */
bool trigger_ble_is_linked(void);

/* Send an ON/OFF/BLINK/STEADY action to one channel. No-op if not linked. */
void trigger_ble_send_action(trg_channel_t ch, trg_action_t act);

/* Set the shared dim register (ui_level 0..255, 255 = brightest) and record
 * the commanded level in trigger_state. No-op if not linked. */
void trigger_ble_send_dim(uint8_t ui_level);

#endif /* TRIGGER_BLE_H */
