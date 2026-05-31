/*
 * Logical state model for the trigger4p AtomS3 status display.
 * The BLE layer feeds parsed notifications + connection events into here;
 * the UI layer reads it to paint the screen. Mutex-protected so the BLE
 * task and UI task don't race.
 */

#ifndef TRIGGER_STATE_H
#define TRIGGER_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "trigger_proto.h"

typedef enum {
    TRG_LINK_BOOT       = 0, /* firmware just booted, BLE not started yet */
    TRG_LINK_DISCOVERY  = 1, /* scanning for the TRIGGER advertisement    */
    TRG_LINK_CONNECTING = 2, /* GATTC open in progress                    */
    TRG_LINK_LINKED     = 3, /* connected + notify subscribed + keepalive */
    TRG_LINK_LOST       = 4, /* was connected, link dropped — retry soon  */
} trg_link_state_t;

void trigger_state_init(void);

void trigger_state_set_link(trg_link_state_t s);
trg_link_state_t trigger_state_get_link(void);

/* Latest decoded channel + blink state (from the FFF7 notification byte).
 * This is REAL device feedback only — the UI's D/P band reads it. Never write
 * commanded/optimistic values here. */
void trigger_state_set_channels(const trg_state_t *st);
void trigger_state_get_channels(trg_state_t *out);

/* Commanded output state (what the button last asked the box to do). The box
 * does not echo dim level and may lag on channel notifications, so the main
 * %-fill hero reads this; the D/P feedback band reads get_channels(). */
void trigger_state_set_command(bool on, bool blink);
void trigger_state_get_command(bool *on, bool *blink);

/* Last commanded UI dim level (0..255). 0xFFFF = unknown (we have not
 * sent a dim frame this session, and the protocol does not expose dim
 * via notifications). */
void     trigger_state_set_dim(uint16_t ui_level);
uint16_t trigger_state_get_dim(void);

/* Connection-error counter — increments every time the box drops out
 * unexpectedly. UI surfaces it in the diagnostic line. */
void     trigger_state_inc_drops(void);
uint32_t trigger_state_get_drops(void);

#endif /* TRIGGER_STATE_H */
