/*
 * TRIGGER 4 Plus protocol — pure frame builders + state-byte parser.
 * See trigger_proto.h for the wire format constants and BLE_PROTOCOL.md
 * in the parent repo for derivation.
 */

#include "trigger_proto.h"

#include <string.h>

/* Per-channel action-byte table.
 * Rows = trg_channel_t, columns = trg_action_t (ON, OFF, BLINK, STEADY).
 * Source: APK fourbottonctl + on-the-wire verification. */
static const uint8_t s_action_table[TRG_CH_COUNT][4] = {
    [TRG_CH1] = { 0xEA, 0xEB, 0xEC, 0xED },
    [TRG_CH2] = { 0xEE, 0xEF, 0xF0, 0xF1 },
    [TRG_CH3] = { 0xF2, 0xF3, 0xF4, 0xF5 },
    [TRG_CH4] = { 0xF6, 0xF7, 0xF8, 0xF9 },
};

static inline uint8_t pwd_hi(uint16_t p) { return (uint8_t)((p >> 8) & 0xFF); }
static inline uint8_t pwd_lo(uint16_t p) { return (uint8_t)(p & 0xFF); }

void trigger_proto_build_action(uint8_t *out, uint8_t device_id, uint16_t password,
                                trg_channel_t ch, trg_action_t act) {
    if (out == NULL) return;
    if ((unsigned)ch >= TRG_CH_COUNT) ch = TRG_CH2;
    if ((unsigned)act > TRG_ACT_STEADY) act = TRG_ACT_OFF;
    uint8_t code = s_action_table[ch][act];

    out[0] = TRG_MAGIC_0;
    out[1] = TRG_MAGIC_1;
    out[2] = device_id;
    out[3] = TRG_OP_ACTION;
    out[4] = code;
    out[5] = TRG_FLAG_ACTION;
    out[6] = pwd_hi(password);
    out[7] = pwd_lo(password);
}

void trigger_proto_build_dim(uint8_t *out, uint8_t device_id, uint16_t password,
                             uint8_t ui_level) {
    if (out == NULL) return;
    /* Wire byte is inverted: 255 = bright -> wire 0x00. Captured phone slider
     * sweeps confirm this. */
    uint8_t inv = (uint8_t)(0xFFu - ui_level);

    out[0] = TRG_MAGIC_0;
    out[1] = TRG_MAGIC_1;
    out[2] = device_id;
    out[3] = TRG_OP_DIM_SET;
    out[4] = inv;
    out[5] = TRG_FLAG_DIM;
    out[6] = pwd_hi(password);
    out[7] = pwd_lo(password);
}

void trigger_proto_build_keepalive(uint8_t *out, uint8_t device_id, uint16_t password) {
    if (out == NULL) return;
    out[0] = TRG_MAGIC_0;
    out[1] = TRG_MAGIC_1;
    out[2] = device_id;
    out[3] = TRG_OP_KEEPALIVE;
    out[4] = 0x00;
    out[5] = TRG_FLAG_ACTION;  /* keepalive shares the 0xDE flag with action */
    out[6] = pwd_hi(password);
    out[7] = pwd_lo(password);
}

void trigger_proto_parse_state(const uint8_t *frame, size_t frame_len, trg_state_t *out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));
    if (frame == NULL || frame_len < 5) {
        out->valid = false;
        return;
    }
    /* Live frames are 6E 00 <state> <b3> <device_id>. Header bytes 0/1 are
     * constant; byte 3 varies on real hardware (seen 0x62 and 0x5D — not a
     * fixed marker, so do NOT gate on it); byte 4 echoes the device id. Only
     * the header is required to trust the state byte. */
    if (frame[0] != 0x6E || frame[1] != 0x00) {
        out->valid = false;
        out->raw_state = (frame_len >= 3) ? frame[2] : 0;
        return;
    }
    uint8_t s = frame[2];
    out->raw_state = s;
    out->valid     = true;
    out->ch1_on    = (s & 0x04) != 0;  /* historical SW1 — APK Ch1 */
    out->ch2_on    = (s & 0x08) != 0;  /* historical SW2 — APK Ch2 */
    out->ch3_on    = (s & 0x10) != 0;
    out->ch4_on    = (s & 0x20) != 0;
    out->ch1_blink = (s & 0x40) != 0;
    out->ch2_blink = (s & 0x80) != 0;
}
