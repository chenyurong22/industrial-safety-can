#!/usr/bin/env python3
"""
Day 13 - Live CAN logger (gs_usb direct, LISTEN-ONLY).

Drives the Innomaker USB2CAN through the gs_usb library directly so it can
run in LISTEN-ONLY mode: it observes the bus without ACKing, so it never
disturbs the two STM32 nodes. Decodes the 0x123 sensor frame and 0x100
heartbeat, re-validates CRC-8 and re-detects counter gaps in Python,
streams to console + CSV.

Run:  python can_logger.py
Stop: Ctrl+C
"""

import csv
import sys
import time

# --- libusb backend: make the bundled lib the default for pyusb -----------
import usb.core
import libusb_package

_BACKEND = libusb_package.get_libusb1_backend()
_orig_find = usb.core.find
def _patched_find(*args, **kwargs):
    kwargs.setdefault("backend", _BACKEND)
    return _orig_find(*args, **kwargs)
usb.core.find = _patched_find

from gs_usb.gs_usb import GsUsb
from gs_usb.gs_usb_frame import GsUsbFrame

# --- gs_usb flags / masks (defined locally to avoid import-path issues) ----
GS_CAN_MODE_LISTEN_ONLY = (1 << 0)
CAN_EFF_FLAG = 0x80000000
CAN_RTR_FLAG = 0x40000000
CAN_ERR_FLAG = 0x20000000
NONE_ECHO_ID = 0xFFFFFFFF

# --- bus / frame configuration --------------------------------------------
BITRATE          = 500000
SENSOR_ID        = 0x123
HEARTBEAT_ID     = 0x100
TEMP_FRAC_SCALE  = 100.0
STATE_NAMES      = {0: "NORMAL", 1: "WARNING", 2: "CRITICAL"}
CSV_PATH         = r"E:\Projects\Industrial Safety Monitoring CAN Network\Python Log Code\can_log.csv"


def crc8(data: bytes) -> int:
    """CRC-8, polynomial 0x07, init 0x00 - same as the STM32 firmware."""
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def open_device() -> GsUsb:
    devs = GsUsb.scan()
    if not devs:
        sys.exit("No gs_usb device found — check Zadig (libusbK/WinUSB) binding.")
    dev = devs[0]
    print(f"gs_usb devices found: {len(devs)} — using index 0")

    if not dev.set_bitrate(BITRATE):
        sys.exit("set_bitrate(500000) failed.")

    # LISTEN-ONLY: observe the bus, never ACK or transmit.
    dev.start(GS_CAN_MODE_LISTEN_ONLY)
    return dev


def drain_stale_frames(dev: GsUsb) -> int:
    """
    Flush any frames already buffered in the adapter's RX FIFO before capture
    begins. Without this, a pre-capture frame becomes the counter baseline and
    the first real frame reports a phantom gap. Reads with a short timeout
    until the FIFO empties (returns False on timeout).
    """
    scratch = GsUsbFrame()
    n = 0
    while dev.read(scratch, 5):   # 5 ms; False once no more buffered frames
        n += 1
        if n > 500:               # safety cap
            break
    return n


def main():
    dev = open_device()
    flushed = drain_stale_frames(dev)
    print(f"Flushed {flushed} stale frame(s) from RX FIFO.")
    print(f"Listening (LISTEN-ONLY) @ {BITRATE} bit/s. Ctrl+C to stop.\n")

    rx = lost = crc_err = hb = 0
    last_counter = None
    t0 = time.time()   # start timing after the drain, so rel_time begins clean

    csv_file = open(CSV_PATH, "w", newline="")
    writer = csv.writer(csv_file)
    writer.writerow([
        "epoch", "rel_time", "can_id", "dlc", "data_hex",
        "type", "counter", "temp", "state", "crc_ok", "gap",
    ])

    frame = GsUsbFrame()
    try:
        while True:
            if not dev.read(frame, 1000):   # timeout in ms -> returns False on timeout
                continue

            cid_raw = frame.can_id
            if cid_raw & CAN_ERR_FLAG:       # bus error frame — skip
                continue
            if frame.echo_id != NONE_ECHO_ID:  # our own echo — skip (shouldn't happen)
                continue

            now = time.time()
            rel = now - t0
            dlc = frame.can_dlc
            data = bytes(frame.data[:dlc])
            hexs = data.hex(" ")
            cid = cid_raw & 0x7FF             # standard 11-bit ID

            ftype = counter = temp = state = crc_ok = gap = ""

            if cid == SENSOR_ID and dlc == 8:
                ftype = "SENSOR"
                rx += 1
                counter = data[0]
                temp = round(data[1] + data[2] / TEMP_FRAC_SCALE, 2)
                state = STATE_NAMES.get(data[3], f"? ({data[3]})")
                crc_ok = (crc8(data[0:7]) == data[7])
                if not crc_ok:
                    crc_err += 1
                gap = 0
                if last_counter is not None:
                    gap = (counter - (last_counter + 1)) & 0xFF
                    lost += gap
                last_counter = counter
                flag = "" if crc_ok else "  <-- CRC FAIL"
                if gap:
                    flag += f"  <-- GAP {gap}"
                print(f"[{rel:8.3f}] SENSOR  cnt={counter:3d} "
                      f"temp={temp:6.2f}C  {state:8s} crc={'OK' if crc_ok else 'BAD'}{flag}")

            elif cid == HEARTBEAT_ID:
                ftype = "HEARTBEAT"
                hb += 1
                print(f"[{rel:8.3f}] HEARTBEAT  [{hexs}]")

            else:
                ftype = "OTHER"
                print(f"[{rel:8.3f}] OTHER   id={cid:#05x} dlc={dlc} [{hexs}]")

            writer.writerow([
                f"{now:.6f}", f"{rel:.6f}", f"{cid:#05x}", dlc, hexs,
                ftype, counter, temp, state, crc_ok, gap,
            ])

    except KeyboardInterrupt:
        pass
    finally:
        dur = time.time() - t0
        csv_file.close()
        try:
            dev.stop()
        except Exception:
            pass
        print("\n--- Summary -------------------------------")
        print(f"Duration        : {dur:8.1f} s")
        print(f"Sensor frames   : {rx}")
        print(f"Lost (gaps)     : {lost}")
        print(f"CRC failures    : {crc_err}")
        if hb:
            print(f"Heartbeats      : {hb}  (avg {dur / hb * 1000:.0f} ms/beat)")
        else:
            print(f"Heartbeats      : 0")
        print(f"Log written to  : {CSV_PATH}")


if __name__ == "__main__":
    main()