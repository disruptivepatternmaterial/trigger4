#!/usr/bin/env python3
"""Drive a TRIGGER ACS Plus 4 relay directly from a Mac (or any bleak host).

Uses CoreBluetooth (via bleak) to talk to the same FFF6 characteristic the
official phone app writes to. Verified against real hardware:
  - Handle 0x0035 / UUID 0xFFF6, properties 0x04 (Write Without Response only).
  - The box rejects opcode 0x12 (Write Request); use opcode 0x52 (Write Cmd).
  - Status notifications come back on FFF7 as 5-byte frames.
  - 200 ms keepalives are required for any multi-step sequence to remain
    accepted by the firmware.

See README.md for the full byte-level reference.

Configure via environment variables (defaults shown):
    TRIGGER_DEVICE_ID=0x44       # byte 2 of every frame
    TRIGGER_PASSWORD=1234        # decimal PIN you set in the TRIGGER app
    TRIGGER_NAME="Trigger 4 Plus"

Usage:
    pip3 install --user bleak
    TRIGGER_PASSWORD=1234 python3 test_trigger_from_mac.py sw1_on
    python3 test_trigger_from_mac.py sw1_off
    python3 test_trigger_from_mac.py sw2_on
    python3 test_trigger_from_mac.py sw2_off
    python3 test_trigger_from_mac.py sw1_blink_on    # blink (turn the
    python3 test_trigger_from_mac.py sw1_blink_off   #  switch ON first;
    python3 test_trigger_from_mac.py sw2_blink_on    #  blink only shows
    python3 test_trigger_from_mac.py sw2_blink_off   #  on a lit channel)
    python3 test_trigger_from_mac.py both_on
    python3 test_trigger_from_mac.py both_off
    python3 test_trigger_from_mac.py both_blink_on
    python3 test_trigger_from_mac.py both_blink_off
    python3 test_trigger_from_mac.py dim 0x80        # set dim level 0..255
    python3 test_trigger_from_mac.py raw 7488XX21EE...   # arbitrary 8/9B
"""
import asyncio
import os
import sys

from bleak import BleakClient, BleakScanner

TRIGGER_NAME = os.environ.get("TRIGGER_NAME", "Trigger 4 Plus")

WRITE_UUID = "0000fff6-0000-1000-8000-00805f9b34fb"   # handle 0x0035
NOTIFY_UUID = "0000fff7-0000-1000-8000-00805f9b34fb"  # handle 0x0038

# int(x, 0) accepts both decimal ("1234") and hex ("0x44") in env vars.
DEVICE_ID = int(os.environ.get("TRIGGER_DEVICE_ID", "0x44"), 0) & 0xFF
PASSWORD  = int(os.environ.get("TRIGGER_PASSWORD",  "1234"), 0) & 0xFFFF
PWD_HI    = (PASSWORD >> 8) & 0xFF
PWD_LO    =  PASSWORD       & 0xFF


def action_frame(action: int) -> bytes:
    """0x21 switch action: ON/OFF/BLINK/STEADY for one channel."""
    return bytes([0x74, 0x88, DEVICE_ID, 0x21, action & 0xFF, 0xDE, PWD_HI, PWD_LO])


def dim_frame(level: int) -> bytes:
    """0x2D set dim level (0..255). Note byte 5 is 0x00, NOT 0xDE."""
    return bytes([0x74, 0x88, DEVICE_ID, 0x2D, level & 0xFF, 0x00, PWD_HI, PWD_LO])


KEEPALIVE = bytes([0x74, 0x88, DEVICE_ID, 0x00, 0x00, 0x00, 0xDE, PWD_HI, PWD_LO])

# action code map (verified for SW1/SW2; SW3/SW4 inferred by contiguous block)
ACTIONS = {
    "sw1_on":         0xEE,
    "sw1_off":        0xEF,
    "sw1_blink_on":   0xF0,
    "sw1_blink_off":  0xF1,
    "sw2_on":         0xF2,
    "sw2_off":        0xF3,
    "sw2_blink_on":   0xF4,
    "sw2_blink_off":  0xF5,
    "sw3_on":         0xF6,
    "sw3_off":        0xF7,
    "sw3_blink_on":   0xF8,
    "sw3_blink_off":  0xF9,
    "sw4_on":         0xFA,
    "sw4_off":        0xFB,
    "sw4_blink_on":   0xFC,
    "sw4_blink_off":  0xFD,
}

# Multi-step macros: list of bytes to write in order, with INTER_S between each.
INTER_S = 0.30
MACROS = {
    "both_on":        [action_frame(ACTIONS["sw1_on"]),        action_frame(ACTIONS["sw2_on"])],
    "both_off":       [action_frame(ACTIONS["sw1_off"]),       action_frame(ACTIONS["sw2_off"])],
    "both_blink_on":  [action_frame(ACTIONS["sw1_blink_on"]),  action_frame(ACTIONS["sw2_blink_on"])],
    "both_blink_off": [action_frame(ACTIONS["sw1_blink_off"]), action_frame(ACTIONS["sw2_blink_off"])],
}

KEEPALIVE_INTERVAL_S = 0.20  # ~200 ms — matches captured app behavior
COMMAND_HOLD_S = 3.0         # how long to keep the connection open after sending


def on_notify(_sender, data: bytearray):
    print(f"  notify FFF7: {data.hex()}")


async def find_trigger():
    print(f"Scanning for '{TRIGGER_NAME}' (10s)...")
    devices = await BleakScanner.discover(timeout=10.0, return_adv=True)
    for addr, (dev, adv) in devices.items():
        name = (adv.local_name or dev.name or "").strip()
        if name == TRIGGER_NAME or "Trigger" in name:
            print(f"  found {name} @ {addr}")
            return dev
    raise SystemExit(f"ERROR: '{TRIGGER_NAME}' not found. Power-cycle the box "
                     f"and make sure no phone is connected.")


async def run_payloads(label: str, payloads):
    dev = await find_trigger()
    print(f"Connecting to {dev.address} ...")
    async with BleakClient(dev) as client:
        await client.start_notify(NOTIFY_UUID, on_notify)
        keepalive_task = asyncio.create_task(_keepalive_loop(client))
        try:
            await asyncio.sleep(0.4)  # let one keepalive go out first
            for i, p in enumerate(payloads):
                print(f"Sending {label}[{i}]: {p.hex()}")
                await client.write_gatt_char(WRITE_UUID, p, response=False)
                if i < len(payloads) - 1:
                    await asyncio.sleep(INTER_S)
            print(f"Holding {COMMAND_HOLD_S}s for relay action + notify ...")
            await asyncio.sleep(COMMAND_HOLD_S)
        finally:
            keepalive_task.cancel()
            try:
                await keepalive_task
            except asyncio.CancelledError:
                pass
            try:
                await client.stop_notify(NOTIFY_UUID)
            except Exception:
                pass
    print("Disconnected.")


async def _keepalive_loop(client: BleakClient):
    while True:
        try:
            await client.write_gatt_char(WRITE_UUID, KEEPALIVE, response=False)
        except Exception as e:
            print(f"  keepalive write failed: {e}")
            return
        await asyncio.sleep(KEEPALIVE_INTERVAL_S)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    cmd = sys.argv[1].lower()
    if cmd == "raw":
        if len(sys.argv) < 3:
            sys.exit("raw needs hex payload, e.g. raw 7488442100DE0000")
        asyncio.run(run_payloads("raw", [bytes.fromhex(sys.argv[2])]))
    elif cmd == "dim":
        if len(sys.argv) < 3:
            sys.exit("dim needs a level 0..255 (decimal or 0xNN)")
        level = int(sys.argv[2], 0) & 0xFF
        asyncio.run(run_payloads(f"dim {level:#x}", [dim_frame(level)]))
    elif cmd in ACTIONS:
        asyncio.run(run_payloads(cmd, [action_frame(ACTIONS[cmd])]))
    elif cmd in MACROS:
        asyncio.run(run_payloads(cmd, MACROS[cmd]))
    else:
        sys.exit(f"Unknown command: {cmd}.\n"
                 f"  Single actions: {', '.join(ACTIONS)}\n"
                 f"  Macros:         {', '.join(MACROS)}\n"
                 f"  Plus:           dim <level>, raw <hex>")


if __name__ == "__main__":
    main()
