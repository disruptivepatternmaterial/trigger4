/*
 * trigger4p AtomS3 — local-secrets template.
 * Copy this file to `secrets.h` and fill in the real values for YOUR
 * TRIGGER 4 Plus unit. `secrets.h` must NEVER be committed (the parent
 * repo .gitignore already excludes the whole atoms3-firmware/ subtree,
 * but treat this as a hard rule independent of git config).
 *
 * The TRIGGER protocol is fully described in the parent repo's
 * README.md and BLE_PROTOCOL.md. The two values below are the only
 * runtime knobs the firmware needs:
 *
 *   TRIGGER_DEVICE_ID  — byte 2 of every command frame ("fourid").
 *                        Factory default observed across captured units
 *                        is 0x44; sniff your own unit if in doubt.
 *
 *   TRIGGER_PASSWORD   — the decimal PIN you set in the official TRIGGER
 *                        app, treated as a 16-bit unsigned integer. The
 *                        firmware splits it into the two payload bytes
 *                        at runtime: bytes 6,7 = (pwd >> 8), (pwd & 0xFF).
 */

#ifndef TRIGGER4P_SECRETS_H
#define TRIGGER4P_SECRETS_H

#define TRIGGER_DEVICE_ID   0x44
#define TRIGGER_PASSWORD    1234

/* Optional: pin the TRIGGER's BLE MAC for faster reconnects. Set to NULL
 * to use name-based discovery ("Trigger 4 Plus") on every connect.
 * Format: 6 hex bytes high-to-low, e.g. {0xAA,0xBB,0xCC,0xDD,0xEE,0xFF}. */
#define TRIGGER_PIN_MAC     NULL

#endif /* TRIGGER4P_SECRETS_H */
