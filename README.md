# TRIGGER ACS Plus 4 — BLE protocol + ESPHome bridge

Reverse-engineered Bluetooth Low Energy protocol for the **TRIGGER ACS Plus**
4-channel BLE relay controller (advertised as `Trigger 4 Plus`), plus:

- a **Python reference client** (`test_trigger_from_mac.py`) you can run from
  any laptop with Bluetooth — no microcontroller required, and
- an **ESPHome firmware** (`trigger4p_esphome.yaml`) that turns an ESP32-C6
  (M5Stack NanoC6, but any ESP32-C6 board works) into a permanent BLE bridge
  exposing the relay to Home Assistant as plain switches and a dim slider.

The protocol write-up below combines Wireshark captures (`nRF52840` +
`nRF Sniffer for Bluetooth LE`), replay from macOS with `bleak`,
decompiled Android APK tables for the four-channel opcode matrix, and weeks of
physical testing on real bumper LEDs—see inline notes where behavior differs
between APK hints and on-unit results (especially dimming).

> **No affiliation with the manufacturer.** Use at your own risk; if you brick
> your unit don't blame me.

---

## Repo contents

| File | What it is |
|------|------------|
| [`README.md`](README.md) | This document — protocol spec + how-to |
| [`trigger4p_esphome.yaml`](trigger4p_esphome.yaml) | ESPHome config for an ESP32-C6 BLE bridge to the relay |
| [`test_trigger_from_mac.py`](test_trigger_from_mac.py) | Python reference client / "protocol oracle" — run from a laptop |
| [`secrets.example.yaml`](secrets.example.yaml) | Template for `secrets.yaml` (gitignored) |
| [`LICENSE`](LICENSE) | MIT |

---

## Quick start

### Option A — Drive the relay from a laptop with Python (no ESP32 needed)

```bash
pip3 install --user bleak
TRIGGER_PASSWORD=1234 \
TRIGGER_DEVICE_ID=0x44 \
python3 test_trigger_from_mac.py sw1_on
```

`TRIGGER_PASSWORD` is the decimal PIN you set in the official TRIGGER app.
`TRIGGER_DEVICE_ID` is byte 2 of every command frame; if you don't know it
yet, the factory default observed in captured units is `0x44` — it's almost
certainly that. The script:

1. scans for the device by name (`Trigger 4 Plus`),
2. opens a connection,
3. starts a 200 ms keepalive loop in the background,
4. writes the requested 8-byte command to characteristic `0xFFF6` with no
   response (opcode `0x52`),
5. enables notify on `0xFFF7` and prints each 5-byte status frame,
6. holds the connection a few seconds, disconnects.

Use it as the **source of truth** when something downstream (ESPHome, HA, an
app) misbehaves — if the Python script can move the relay and your firmware
can't, the bug is in the firmware, not in the protocol.

Available commands:

```text
sw1_on  sw1_off  sw1_blink_on  sw1_blink_off
sw2_on  sw2_off  sw2_blink_on  sw2_blink_off
sw3_on  sw3_off  sw3_blink_on  sw3_blink_off    # APK Ch4 (F6–F9)
sw4_on  sw4_off  sw4_blink_on  sw4_blink_off    # APK Ch1 (EA–ED); naming quirk — see action table
both_on  both_off  both_blink_on  both_blink_off
dim <level>          # UI brightness 0..255 (script sends wire byte 0xFF−level)
raw  <hexbytes>      # arbitrary **8-byte** frame, e.g. `raw 748844210EEDE2425`
```

### Option B — ESPHome ESP32-C6 BLE bridge (Home Assistant)

1. Copy `secrets.example.yaml` to `secrets.yaml` and fill in WiFi creds,
   API/OTA keys, the relay's BLE MAC, the device-ID byte, and the PIN as a
   plain decimal integer (the firmware splits it into the two payload bytes
   for you).
2. First flash via USB:
   ```bash
   esphome run trigger4p_esphome.yaml --device /dev/cu.usbmodem1101
   ```
3. After that, OTA from the Home Assistant ESPHome add-on (or `esphome run …
   --device <ip>`).

What you get in HA:

| Entity | Effect |
|--------|--------|
| `switch.passenger_light` | SW1 ON/OFF (action codes `0xEE`/`0xEF`) |
| `switch.driver_light`    | SW2 ON/OFF (`0xF2`/`0xF3`) |
| `switch.passenger_blink` | SW1 blink mode (`0xF0`/`0xF1`) |
| `switch.driver_blink`    | SW2 blink mode (`0xF4`/`0xF5`) |
| `switch.both_lights`     | Master — state derives from both children, turn-on/off cascades to children with 300 ms inter-write spacing |
| `switch.both_blink`      | Master for blink — same pattern |
| `number.dim_level`       | **51–255** in steps of **51** (five discrete levels). Writes opcode `0x2D` with wire byte **`0xFF − slider`** (matches the official app). Dim is a **single shared register** on tested firmware — only `0x2D` produced a visible change; APK hints at `0x2B`/`0x2C`/`0x2E` per-channel opcodes, but they were **no-ops** on the unit used to validate this repo. |
| `sensor` (internal) | **`TRIGGER Notify`** — `ble_client` characteristic on `FFF7` with `notify: true`. Keeps the notification stream flowing so the box does not throttle writes (**required**; see note at end of this README). |
| `light.trigger_status_led` | Onboard NanoC6 RGB LED — orange on boot, red when BLE link is down, green when linked |
| `binary_sensor.trigger_test_button` | Onboard GPIO9 — **single click** toggles `both_lights` (sets dim to **255** when turning on); **double click** toggles `both_blink`; **hold ≥ ~1 s** cycles dim **51 → 102 → … → 255 → 51** |

---

## Protocol summary (TL;DR)

- **Service:** `0xFFF0`
- **Write characteristic:** `0xFFF6`, handle `0x0035`, **Write Without Response only** (opcode `0x52`). The box rejects `0x12` (Write Request) with ATT error `0x03 "Write Not Permitted"` — that's the original "ESPHome says success but the relay never moves" trap.
- **Notify characteristic:** `0xFFF7`, handle `0x0038` — short (typically **5-byte**) status frames; **your Central should subscribe** (official app does ~every 250 ms worth of traffic).
- **Action frame (8 bytes):** `74 88 <id> 21 <action> DE <pwd_hi> <pwd_lo>`
- **Dim frame (8 bytes):** `74 88 <id> 2D <inv> 00 <pwd_hi> <pwd_lo>` where **`inv = 0xFF − UI_level`** and UI_level is what humans mean by brightness (**255 = brightest** ⇒ wire byte `0x00`; lower UI values ⇒ larger wire bytes ⇒ dimmer).
- **Keepalive (8 bytes, every ~200 ms — REQUIRED):** `74 88 <id> 00 00 DE <pwd_hi> <pwd_lo>` — **matches captured official-app traffic.** An older 9-byte variant (`… 00 00 00 DE …`) **desynchronized** the parser on real hardware and caused dim writes to corrupt unrelated channel state (strobe/hunt).
- **Action codes (APK `fourbottonctl` + wire check):** four contiguous blocks — **ch1** `EA–ED`, **ch2** `EE–F1`, **ch3** `F2–F5`, **ch4** `F6–F9` (ON, OFF, BLINK, STEADY each). The bundled ESPHome example maps **passenger → ch2**, **driver → ch3**.
- **No "send each command twice" in captures.** The periodic traffic every ~200 ms is the **keepalive**, not command retries.

Full byte-level reference is in the rest of this document.

---

## BLE protocol — full reference

### Connection layer

| | |
|---|---|
| Advertising name | `Trigger 4 Plus` |
| Advertising address | Public BLE address with a Texas Instruments OUI (varies per unit) |
| Advertised service | `0xFFF0` |
| MTU (server) | 23 — fits the **8-byte** command frames natively |
| Pairing/bonding | none — open characteristic, no encryption |

The peripheral only advertises while no Central is connected. If the phone
app (or any other Central) is currently connected, the box is silent on the
air and will not appear in scans. If a sniffer never sees it, force-quit the
phone app first.

### GATT characteristics inside service `0xFFF0`

| UUID | Handle | Properties | Used for |
|------|--------|------------|----------|
| `0xFFF1` | 0x0025 | Read + Notify (0x0A) | (unused by command path) |
| `0xFFF2` | 0x0028 | Read (0x02) | (unused) |
| `0xFFF3` | 0x002B | Write w/response (0x08) | **NOT** the command path on this firmware |
| `0xFFF4` | 0x002E | Notify (0x10) | (unused) |
| `0xFFF5` | 0x0032 | Read (0x02) | (unused) |
| **`0xFFF6`** | **0x0035** | **Write WITHOUT response (0x04)** | **Command channel — all writes go here** |
| **`0xFFF7`** | **0x0038** | **Notify (0x10)** | **Status feedback — every write triggers a notification** |
| `0xFFF8` | 0x003C | Write w/o response (0x04) | (unused) |

**Critical correction vs older / guess-based docs:** `0xFFF6` only exposes
property bit `0x04` (Write Without Response). It will reject opcode `0x12`
(Write Request) with ATT error `0x03 "Write Not Permitted"`. The phone app
uses opcode **`0x52` (Write Command, no response)**. In code:

- ESP-IDF: `esp_ble_gattc_write_char(..., ESP_GATT_WRITE_TYPE_NO_RSP, ...)`
- ESPHome `ble_client.ble_write`: default behavior is correct; do **not**
  pass `response: true`.
- Bleak: `await client.write_gatt_char(WRITE_UUID, payload, response=False)`.

### Command frame formats

All commands are written to `0xFFF6` (handle `0x0035`). On captured **Trigger
4 Plus** traffic with the stock app, **every payload is 8 bytes**.

#### 8-byte action frame (discrete commands)

```
74 88 <device_id> <opcode> <payload_b4> <flag> <pwd_hi> <pwd_lo>
```

| Byte | Value | Meaning |
|------|-------|---------|
| 0 | `0x74` | fixed magic |
| 1 | `0x88` | fixed magic |
| 2 | device ID | the unit's "fourid" (any 0–255). Default observed: `0x44` |
| 3 | opcode | `0x21` = switch action; `0x2D` = dim; `0x2B` = dim-slider hint (see below) |
| 4 | payload | action byte for `0x21`; **inverted dim** `0xFF − UI` for `0x2D` |
| 5 | flag | `0xDE` for opcode `0x21` and **keepalive**; `0x00` for `0x2D` / `0x2B` |
| 6 | `(password >> 8) & 0xFF` | high byte of PIN |
| 7 | `password & 0xFF` | low byte of PIN |

The PIN is the value you typed into the TRIGGER app's "Password" field,
treated as a single 16-bit decimal integer. Example: PIN `1234` → bytes
`0x04 0xD2`.

#### 8-byte keepalive frame (REQUIRED)

```
74 88 <device_id> 00 00 DE <pwd_hi> <pwd_lo>
```

The official app emits this every ~150–300 ms for the whole session.
**The relay considers a session "alive" only while these arrive.** Without
them, multi-step sequences (dim ramps, rapid toggles) start failing within a
few seconds.

`trigger4p_esphome.yaml` mirrors that with a `200ms` `interval:` that writes
this frame whenever `trigger_link_up` is true.

**Historical mistake:** an earlier revision used a **9-byte** keepalive
(`… 00 00 00 DE …`). On hardware used to validate this project, that extra
`0x00` **shifted the parser** so subsequent dim frames corrupted unrelated
action bytes (symptoms: wrong channel strobing, passenger dropping out).
Stick to **8 bytes**.

During connection setup, captures occasionally show `74 88 00 00 00 00 DE …`
(device-ID byte forced to `00`). It is not required for reliable control when
using the 8-byte keepalive above with the real device ID.

### Action codes for opcode `0x21` (switch actions)

Each output channel uses one contiguous block of four bytes: **ON, OFF,
BLINK-on, STEADY** (clear blink). Source order in the decompiled Android APK
(`fourbottonctl`) plus on-wire verification:

| APK channel | ON | OFF | BLINK on | STEADY | Notes |
|-------------|----|-----|----------|--------|-------|
| **Ch1** | `0xEA` | `0xEB` | `0xEC` | `0xED` | |
| **Ch2** | `0xEE` | `0xEF` | `0xF0` | `0xF1` | mapped as **passenger** in `trigger4p_esphome.yaml` |
| **Ch3** | `0xF2` | `0xF3` | `0xF4` | `0xF5` | mapped as **driver** |
| **Ch4** | `0xF6` | `0xF7` | `0xF8` | `0xF9` | |

In `test_trigger_from_mac.py`, historical convenience names are **`sw1` =
Ch2**, **`sw2` = Ch3**, **`sw3` = Ch4**, **`sw4` = Ch1** (only the first two
are exposed as HA switches in the reference YAML).

Notes on blink:

- "Blink on" turns on a per-channel **mode bit**. The channel must already
  be ON solid for the blink to be visible. If the channel is OFF when you
  send the blink code, the bit gets set in the state register but nothing
  happens until you turn the channel on.
- Blink mode persists across other commands until explicitly cleared with
  the matching "blink off / steady" code.
- Turning the channel OFF (e.g. `0xEF` for SW1) does **not** necessarily
  clear the blink bit. If you want a clean state, send `<steady>` then
  `<off>`.

### Opcode `0x2D` — set dim level (global)

```
74 88 <device_id> 2D <inv> 00 <pwd_hi> <pwd_lo>
```

- Byte 4 is **`inv = 0xFF − UI_level`** where **UI_level** is what you surface
  in Home Assistant or pass to `test_trigger_from_mac.py dim …`:
  **255 = brightest** ⇒ wire `0x00`; dimmer UI values ⇒ larger wire bytes.
  Captured phone slider sweeps land on the inverted wire byte, not on raw “higher
  = brighter.”
- Byte 5 is always `0x00` for dim (`0xDE` is only for `0x21` / keepalive).
- **Single shared dim:** only **`0x2D`** produced a visible brightness change on
  the hardware used to lock this repo. The APK references **`0x2B`–`0x2E`**
  “per-channel” dim opcodes for 4-PLUS; **`0x2C` was a no-op** when exercised in
  isolation — treat per-channel dim as **toolchain/UI residue**, not something
  you can rely on across units until you sniff your own box.
- With **both** channels ON, changing dim adjusts a shared PWM register; you
  may see the “other” output breathe or shimmer slightly when it is not the item
  you touched in HA — distinct from the **FFF7-notify bug**, which caused hard
  strobing until notifications were subscribed.

### Opcode `0x2B` — dim slider state markers

```
74 88 <device_id> 2B <param> 00 <pwd_hi> <pwd_lo>
```

Three values seen in the wild: `0x00`, `0x1E`, `0x20`, all clustered at the
moment the slider was first touched. Treated by the box as a "begin slider
drag" / "release" hint. **Sending `0x2D <level>` directly works without ever
sending `0x2B` first** — confirmed by Mac replay.

### Status notifications on `0xFFF7`

After every write to `0xFFF6` the box notifies on `0xFFF7` with a 5-byte
payload:

```
6E 00 <state_byte> 62 44
```

The state byte is a bitmap of currently-active channels and blink modes:

| Bit | Mask | Meaning |
|-----|------|---------|
| 2 | `0x04` | SW1 ON |
| 3 | `0x08` | SW2 ON |
| 4 | `0x10` | SW3 ON (inferred) |
| 5 | `0x20` | SW4 ON (inferred) |
| 6 | `0x40` | SW1 BLINK active |
| 7 | `0x80` | SW2 BLINK active |

Verified examples (PIN bytes redacted as `XX XX`):

| Sequence | Notify | State byte breakdown |
|----------|--------|----------------------|
| SW1 ON | `6e 00 04 62 44` | `0x04` = SW1 |
| SW1 ON, SW2 ON | `6e 00 0c 62 44` | `0x0C` = SW1 + SW2 |
| SW1 ON, SW2 ON, SW1 blink | `6e 00 4c 62 44` | `0x4C` = SW1 + SW2 + SW1-blink |
| SW2 ON, SW2 blink | `6e 00 88 62 44` | `0x88` = SW2 + SW2-blink |
| all off | `6e 00 00 62 44` | `0x00` |

Bytes 0, 1, 3, 4 are constants in the captured data and have not been
decoded beyond "header / footer".

### Reference 8-byte commands (with placeholder PIN bytes)

Replace `<id>` with your unit's device-ID byte and `<HI> <LO>` with the high
and low bytes of your PIN.

| Operation | Hex |
|-----------|-----|
| SW1 ON | `74 88 <id> 21 EE DE <HI> <LO>` |
| SW1 OFF | `74 88 <id> 21 EF DE <HI> <LO>` |
| SW1 blink | `74 88 <id> 21 F0 DE <HI> <LO>` |
| SW1 steady (clear blink) | `74 88 <id> 21 F1 DE <HI> <LO>` |
| SW2 ON | `74 88 <id> 21 F2 DE <HI> <LO>` |
| SW2 OFF | `74 88 <id> 21 F3 DE <HI> <LO>` |
| SW2 blink | `74 88 <id> 21 F4 DE <HI> <LO>` |
| SW2 steady | `74 88 <id> 21 F5 DE <HI> <LO>` |
| Set UI dim ≈ mid (UI `128` ⇒ wire `0x7F`) | `74 88 <id> 2D 7F 00 <HI> <LO>` |
| Set UI dim full bright (255 ⇒ wire `0x00`) | `74 88 <id> 2D 00 00 <HI> <LO>` |
| Keepalive (every ~200 ms) | `74 88 <id> 00 00 DE <HI> <LO>` |

---

## Important behavioral notes

- **Subscribe `0xFFF7`.** Third-party firmware must enable BLE notifications
  on **`FFF7`** while connected (the bundled ESPHome uses an internal
  `ble_client` sensor). Skipping this caused **severe cross-talk while dimming**
  on validated hardware until the notify stream was opened.
- **WiFi gives up after 60 s.** If the C6 boots and can't associate with
  WiFi within 60 s, the firmware disables WiFi entirely so the shared
  2.4 GHz radio belongs to BLE. Power-cycle to retry WiFi. This is the
  difference between "BLE works reliably" and "BLE never sees the TRIGGER
  advertise" — the WiFi scanner will starve BLE on the C6 if left running
  with bad creds.
- **The TRIGGER box will not advertise while another Central is connected.**
  Force-close the phone TRIGGER app before expecting the C6 (or any
  sniffer) to see it.
- **Master switches reflect their children, not the other way around.** The
  state of `switch.both_lights` = `passenger_light AND driver_light`.
  Toggling the master cascades to the children with a 300 ms gap; toggling
  a single child updates the master automatically.
- **Dim is global (`0x2D` only)** on the validated unit — the HA slider is
  intentionally coarse (**51–255**, step **51**) to avoid diving into unusably
  low duty cycles on long LED runs. Tune in YAML if your loads behave
  differently.

---

## Implementing your own bridge (ESPHome, scripts, microcontrollers)

Minimal checklist that matches **validated** behavior on at least one Trigger
4 Plus unit:

1. **Write Without Response** only on **`FFF6`** (BLE opcode `0x52`).
2. **Subscribe to notify on `FFF7`** for the whole session — do not leave this
   characteristic idle while spamming writes.
3. Emit the **8-byte** keepalive (`74 88 <id> 00 00 DE …`) every **~200 ms**
   while connected — **not** the obsolete 9-byte form.
4. For opcode **`0x2D`**, send **`inv = 0xFF − UI_brightness`** in byte 4;
   byte 5 must be **`0x00`**.
5. Serialize bursts (ESPHome example uses **`mode: queued`** scripts + **300 ms**
   spacing between paired channel writes).

The reference firmware is `trigger4p_esphome.yaml` — copy `secrets.example.yaml`
to `secrets.yaml` and adjust GPIO/pin definitions if you move off the NanoC6.

---

## Hardware

The reference build uses an [M5Stack NanoC6](https://shop.m5stack.com/products/m5nanoc6)
because it's $9, has WiFi + BLE on the ESP32-C6, an onboard WS2812 status LED
(GPIO20, gated by GPIO19), and an onboard user button (GPIO9). Any ESP32-C6
dev board works — you'll just lose the onboard status LED + button. Plain
ESP32, ESP32-S3, etc. work too if you swap the `esp32.board:` line.

---

## Capturing your own traffic

If you've got a different unit and want to verify the protocol on it:

1. Get a [Nordic nRF52840 USB Dongle](https://www.nordicsemi.com/Products/Development-hardware/nRF52840-Dongle)
   (~$10).
2. Flash it with `nRF Sniffer for Bluetooth LE` (Nordic's official
   firmware) and install the matching `extcap` plugin into Wireshark.
3. Force-quit the TRIGGER app on your phone, power-cycle the relay so it
   advertises, then start a capture filtered to its MAC.
4. Reconnect with the phone app and operate every switch / blink / dim
   slider position you want to characterize.
5. In Wireshark, look for ATT writes to handle `0x0035` and the matching
   notifications on `0x0038`.

Then either point `test_trigger_from_mac.py` at your unit (just set
`TRIGGER_PASSWORD` and `TRIGGER_DEVICE_ID` in the env) or use its `raw`
subcommand to replay arbitrary captured frames before committing to firmware.

---

## License

MIT — see [`LICENSE`](LICENSE).

This repo contains **no manufacturer code**. The protocol description is
derived from BLE captures, replay with Python/`bleak`, decompiled Android
APK resources for opcode/action tables, and physically operating the relay.
