/*
 * TRIGGER 4 Plus protocol — pure functions: frame builders + state byte parser.
 * No I/O. The BLE layer (trigger_ble.c) calls into here to format payloads
 * and to decode the 5-byte FFF7 notification frames.
 *
 * Reference: parent repo's README.md and BLE_PROTOCOL.md.
 */

#ifndef TRIGGER_PROTO_H
#define TRIGGER_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Wire-level constants — magic bytes 0/1, flag-byte values for opcode 0x21
 * (action) / opcodes 0x2D and 0x2B (dim + dim-slider hint). */
#define TRG_MAGIC_0         0x74
#define TRG_MAGIC_1         0x88
#define TRG_FLAG_ACTION     0xDE  /* opcode 0x21 + keepalive */
#define TRG_FLAG_DIM        0x00  /* opcode 0x2D / 0x2B      */

/* Opcodes. */
#define TRG_OP_KEEPALIVE    0x00
#define TRG_OP_ACTION       0x21
#define TRG_OP_DIM_HINT     0x2B
#define TRG_OP_DIM_SET      0x2D

/* Action codes per 4-channel block (ON, OFF, BLINK, STEADY) — APK-derived,
 * verified against captured wire traffic. The reference YAML maps Ch2 to
 * "passenger" and Ch3 to "driver". */
typedef enum {
    TRG_CH1 = 0, /* APK channel 1 — wire codes 0xEA..0xED */
    TRG_CH2 = 1, /* APK channel 2 — wire codes 0xEE..0xF1 (passenger) */
    TRG_CH3 = 2, /* APK channel 3 — wire codes 0xF2..0xF5 (driver)    */
    TRG_CH4 = 3, /* APK channel 4 — wire codes 0xF6..0xF9             */
    TRG_CH_COUNT
} trg_channel_t;

typedef enum {
    TRG_ACT_ON     = 0,
    TRG_ACT_OFF    = 1,
    TRG_ACT_BLINK  = 2,  /* set blink mode bit (channel must be ON to be visible) */
    TRG_ACT_STEADY = 3   /* clear blink mode bit */
} trg_action_t;

/* All command frames are 8 bytes on this firmware — 9-byte variants caused
 * parser desync on captured hardware (see README.md "Historical mistake"). */
#define TRG_FRAME_LEN       8

/* Build the 8-byte action frame:
 *   74 88 <id> 21 <action> DE <pwd_hi> <pwd_lo>
 * `out` must point to at least TRG_FRAME_LEN bytes. */
void trigger_proto_build_action(uint8_t *out, uint8_t device_id, uint16_t password,
                                trg_channel_t ch, trg_action_t act);

/* Build the 8-byte dim-set frame:
 *   74 88 <id> 2D <inv> 00 <pwd_hi> <pwd_lo>   inv = 0xFF - ui_level
 * ui_level: 0..255, 255 = brightest. */
void trigger_proto_build_dim(uint8_t *out, uint8_t device_id, uint16_t password,
                             uint8_t ui_level);

/* Build the 8-byte keepalive frame:
 *   74 88 <id> 00 00 DE <pwd_hi> <pwd_lo>
 * Required every ~200 ms while connected. */
void trigger_proto_build_keepalive(uint8_t *out, uint8_t device_id, uint16_t password);

/* Decoded state from a 5-byte FFF7 notification (`6E 00 <state> <variant> <id>`).
 * The state bits use the app/test-script SW order: SW1 is APK Ch2/passenger,
 * SW2 is APK Ch3/driver. */
typedef struct {
    bool ch1_on;       /* APK Ch1 — bit 5 (0x20), inferred SW4 */
    bool ch2_on;       /* APK Ch2 / passenger — bit 2 (0x04), SW1 */
    bool ch3_on;       /* APK Ch3 / driver    — bit 3 (0x08), SW2 */
    bool ch4_on;       /* APK Ch4 — bit 4 (0x10), inferred SW3 */
    bool ch2_blink;    /* APK Ch2 / passenger blink — bit 6 (0x40), SW1 */
    bool ch3_blink;    /* APK Ch3 / driver blink    — bit 7 (0x80), SW2 */
    uint8_t raw_state; /* full state byte for unknown-bit display */
    bool valid;        /* false = unparseable / wrong header      */
} trg_state_t;

/* Parse a 5-byte status notification. `frame` must be at least 5 bytes. */
void trigger_proto_parse_state(const uint8_t *frame, size_t frame_len, trg_state_t *out);

#endif /* TRIGGER_PROTO_H */
