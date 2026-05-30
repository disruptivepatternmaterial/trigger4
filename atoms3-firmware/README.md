# trigger4p — M5Stack AtomS3 standalone controller

Native ESP-IDF firmware that turns an **M5Stack AtomS3** (0.85" 128×128 LCD +
single front button) into a handheld BLE remote/status display for a **TRIGGER
ACS Plus 4-channel** relay box. No phone, no Home Assistant, no Wi-Fi.

It speaks the exact BLE protocol reverse-engineered in the parent repo
(`../README.md`, `../BLE_PROTOCOL.md`) and verified on hardware via
`../test_trigger_from_mac.py` and `../trigger4p_esphome.yaml`. The command
frames this firmware emits are byte-for-byte identical to those two proven
implementations.

---

## Hardware

| Item        | Value                                             |
| ----------- | ------------------------------------------------- |
| Board       | M5Stack AtomS3 (ESP32-S3, GC9107 128×128 LCD)     |
| Button      | Front "screen" button on GPIO41 (active-low)      |
| Radio       | BLE 4.2 GATT **client** (Bluedroid)               |
| Target      | TRIGGER ACS Plus 4 (advertises `Trigger 4 Plus`)  |

## What it does

- Scans for the TRIGGER box by advertised name (`Trigger 4 Plus`), or by a
  pinned MAC if you set `TRIGGER_PIN_MAC` in `main/secrets.h`.
- Opens GATT service `0xFFF0`, resolves write char `0xFFF6` and notify char
  `0xFFF7`, subscribes to notifications, and runs the required 200 ms keepalive.
- Renders link state (`BOOT` / `SCANNING` / `CONNECTING` / `LINKED` / `LOST`),
  the four channel pills (ON / OFF / BLINK), and the last commanded dim level.
- Drives the relay from the single button (see **Controls**).

## Controls (single front button)

Mirrors the button UX in `../trigger4p_esphome.yaml`. The "both" channels are
the common two-output wiring: passenger = APK ch2 (`0xEE/0xEF`), driver = APK
ch3 (`0xF2/0xF3`).

| Gesture            | Action                                              |
| ------------------ | --------------------------------------------------- |
| Short press        | Toggle both lights on/off (on resets dim to full)   |
| Double press       | Toggle both lights into/out of blink mode           |
| Long press (≥0.8 s) | Cycle dim level 51 → 102 → 153 → 204 → 255 → 51     |

All button actions are no-ops until the link is `LINKED`.

## BLE protocol (as implemented here)

All command frames are **8 bytes**, written to `0xFFF6` with **Write Without
Response** (opcode `0x52` — the box rejects Write Request `0x12`).

```
action     74 88 <id> 21 <action> DE <pwd_hi> <pwd_lo>
dim        74 88 <id> 2D <inv>    00 <pwd_hi> <pwd_lo>   inv = 0xFF - ui_level
keepalive  74 88 <id> 00 00       DE <pwd_hi> <pwd_lo>   every 200 ms
```

Action codes (verified against the official APK + on-wire capture):

| Channel       | ON   | OFF  | BLINK | STEADY |
| ------------- | ---- | ---- | ----- | ------ |
| ch1           | EA   | EB   | EC    | ED     |
| ch2 passenger | EE   | EF   | F0    | F1     |
| ch3 driver    | F2   | F3   | F4    | F5     |
| ch4           | F6   | F7   | F8    | F9     |

State notifications arrive on `0xFFF7` as 5-byte frames `6E 00 <state> 62 44`;
`trigger_proto_parse_state()` decodes the channel/blink bits.

## Configure

Copy the template and set your unit's values (this file is **gitignored**):

```bash
cp main/secrets.example.h main/secrets.h
```

```c
#define TRIGGER_DEVICE_ID   0x44   /* byte 2 of every frame; factory default */
#define TRIGGER_PASSWORD    1234   /* decimal PIN set in the official app     */
/* Optional, faster reconnect to one specific box:
   #define TRIGGER_PIN_MAC  { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF } */
```

## Build & flash

PlatformIO (pinned to `platform-espressif32 @ ~6.11.0`, ESP-IDF 5.4):

```bash
pio run -e m5atoms3                                   # build
pio run -e m5atoms3 -t upload --upload-port /dev/cu.usbmodemXXXX
```

The AtomS3 enumerates as a USB CDC port (`/dev/cu.usbmodem*` on macOS).
`pio device monitor` needs a real TTY; if you are scripting, read the port with
pyserial instead.

## Source layout

| File                | Responsibility                                        |
| ------------------- | ----------------------------------------------------- |
| `main/app_main.c`   | Boot, main loop, button classify → command dispatch   |
| `main/trigger_ble.c`| GATT client: scan, connect, notify, keepalive, send   |
| `main/trigger_proto.c` | Pure frame builders + 5-byte state parser          |
| `main/trigger_state.c` | Mutex-guarded link/channel/dim model               |
| `main/trigger_ui.c` | M5GFX diff-based 128×128 renderer                     |
| `main/m5atoms3_*`   | Display + button HAL                                  |

## Verification status

Per the repo's "no aspirational docs" rule — this lists exactly what has been
observed, not what the code _should_ do.

| Claim | Status |
| ----- | ------ |
| Compiles clean on platform-espressif32 6.11.0 / IDF 5.4.1 | ✅ Verified (`[SUCCESS]`, 2026-05-29) |
| Boots, inits M5GFX + BLE, starts scanning for `Trigger 4 Plus` | ✅ Verified on hardware (serial log) |
| Connects, reaches `LINKED`, and holds the box with the 200 ms keepalive | ✅ Verified on hardware (2026-05-29, serial: `Found 'Trigger 4 Plus' … connecting` → `service 0xFFF0 handles 35..` → `write char 0xFFF6 = 0x0035` → `CCCD write result: status=0` → `link is up`). Box also stops advertising and stays held across repeated host scans for 40 s+, which can't happen unless the keepalive is running. |
| Commands transmit over the live link (action + dim) | ✅ Verified on hardware via a built-in self-test (`-DTRG_SELFTEST=1`): on link it sent `dim→00`, both ON (`EE`,`F2`), both OFF (`EF`,`F3`); every `esp_ble_gattc_write_char` returned `ESP_OK` over the same channel the accepted keepalive uses. |
| LEDs physically change | ⚠️ **Not directly observed** (no camera on the bumper from the bench). High confidence: the action/dim bytes are identical to the verified `../test_trigger_from_mac.py` / `../trigger4p_esphome.yaml` and use the same authenticated path as the keepalive the box accepts. Eyeball the lights on first button use. |
| On-screen channel pills reflect **live** box state | ❌ **Not working as a live mirror.** The CCCD subscription succeeds (`status=0`) but the box sent **zero** `0xFFF7` notifications in 18 s of testing, including right after BLE-initiated channel changes. The box appears to only notify on its own physical-button changes, not BLE-commanded ones. The pills therefore track what we *send*, not authoritative box state. Treat the display as a command echo, not a sensor. |

### Historical note (the bugs that made it "do nothing")

Three separate defects, all fixed 2026-05-29:

1. **Never compiled.** Called the non-existent `esp_ble_gap_start_scan` (real
   IDF API: `esp_ble_gap_start_scanning`) and defined `TRIGGER_PIN_MAC NULL`,
   which breaks the `#if` preprocessor guard. Two hard compile errors — so no
   working binary was ever produced.
2. **Never connected.** The scanner parsed only the primary advertising packet
   (`adv_data_len` bytes). The TRIGGER box advertises its **name in the scan
   response**, so the name never matched and it scanned forever. Fixed by using
   `esp_ble_resolve_adv_data`, which searches the combined adv + scan-response
   buffer.
3. **Never controlled anything.** The prior build was a read-only status
   display that only transmitted keepalives. The single button is now wired to
   send action/dim frames (see **Controls**).
