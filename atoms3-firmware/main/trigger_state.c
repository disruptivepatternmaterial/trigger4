/*
 * Logical state model for the trigger4p AtomS3 status display.
 * Mutex-protected accessors for the BLE task / UI task split.
 */

#include "trigger_state.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

static SemaphoreHandle_t s_mtx        = NULL;
static trg_link_state_t  s_link       = TRG_LINK_BOOT;
static trg_state_t       s_channels   = { .valid = false };
static uint16_t          s_dim_level  = 0xFFFF;  /* unknown until first set */
static uint32_t          s_drop_count = 0;

static void take(void)  { if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY); }
static void give(void)  { if (s_mtx) xSemaphoreGive(s_mtx); }

void trigger_state_init(void) {
    if (s_mtx == NULL) {
        s_mtx = xSemaphoreCreateMutex();
    }
}

void trigger_state_set_link(trg_link_state_t s) {
    take(); s_link = s; give();
}

trg_link_state_t trigger_state_get_link(void) {
    take(); trg_link_state_t s = s_link; give(); return s;
}

void trigger_state_set_channels(const trg_state_t *st) {
    if (st == NULL) return;
    take(); s_channels = *st; give();
}

void trigger_state_get_channels(trg_state_t *out) {
    if (out == NULL) return;
    take(); *out = s_channels; give();
}

void trigger_state_set_dim(uint16_t ui_level) {
    take(); s_dim_level = ui_level; give();
}

uint16_t trigger_state_get_dim(void) {
    take(); uint16_t v = s_dim_level; give(); return v;
}

void trigger_state_inc_drops(void) {
    take(); s_drop_count++; give();
}

uint32_t trigger_state_get_drops(void) {
    take(); uint32_t v = s_drop_count; give(); return v;
}
