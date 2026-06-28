#!/usr/bin/env python3
"""
Day 13 - Offline report generator. Reads can_log.csv and produces a
4-panel matplotlib report: temperature over time, heartbeat interval
histogram, inter-frame timing, and a loss/CRC-error timeline.

Run: python can_report.py
Out: can_report.png  (+ printed summary, incl. bus-load estimate)
"""

import sys

import matplotlib.pyplot as plt
import pandas as pd

CSV_PATH    = "can_log.csv"
OUT_PATH    = "can_report.png"
HB_TARGET   = 200.0     # ms, expected heartbeat period
BITRATE     = 500000    # for bus-load estimate
BITS_PER_FRAME = 130    # ~ standard 8-byte CAN frame incl. stuffing/overhead


def main():
    try:
        df = pd.read_csv(CSV_PATH)
    except FileNotFoundError:
        sys.exit(f"{CSV_PATH} not found - run can_logger.py first.")

    if df.empty:
        sys.exit("Log is empty - nothing to plot.")

    sensors = df[df["type"] == "SENSOR"].copy()
    beats   = df[df["type"] == "HEARTBEAT"].copy()
    duration = df["rel_time"].max() - df["rel_time"].min()

    fig, ax = plt.subplots(2, 2, figsize=(13, 8))
    fig.suptitle("Industrial Safety CAN Network - Bus Report (Day 13)",
                 fontsize=14, fontweight="bold")

    # 1) Temperature over time
    if not sensors.empty:
        ax[0, 0].plot(sensors["rel_time"], sensors["temp"], lw=1.0)
        ax[0, 0].set_title("Temperature over time")
        ax[0, 0].set_xlabel("Time (s)")
        ax[0, 0].set_ylabel("Temp (C)")
        ax[0, 0].grid(True, alpha=0.3)

    # 2) Heartbeat interval histogram
    if len(beats) > 1:
        intervals = beats["rel_time"].diff().dropna() * 1000.0
        ax[0, 1].hist(intervals, bins=30, color="tab:green", alpha=0.8)
        ax[0, 1].axvline(HB_TARGET, color="red", ls="--",
                         label=f"target {HB_TARGET:.0f} ms")
        ax[0, 1].set_title("Heartbeat interval")
        ax[0, 1].set_xlabel("Interval (ms)")
        ax[0, 1].set_ylabel("Count")
        ax[0, 1].legend()
        ax[0, 1].grid(True, alpha=0.3)

    # 3) Inter-frame timing (sensor frames)
    if len(sensors) > 1:
        gaps = sensors["rel_time"].diff().dropna() * 1000.0
        ax[1, 0].hist(gaps, bins=30, color="tab:blue", alpha=0.8)
        ax[1, 0].set_title("Sensor inter-frame timing")
        ax[1, 0].set_xlabel("Gap between frames (ms)")
        ax[1, 0].set_ylabel("Count")
        ax[1, 0].grid(True, alpha=0.3)

    # 4) Loss / CRC-error timeline
    if not sensors.empty:
        ax[1, 1].plot(sensors["rel_time"], sensors["counter"],
                      lw=0.8, color="gray", label="counter")
        losses = sensors[sensors["gap"].fillna(0) > 0]
        crcbad = sensors[sensors["crc_ok"] == False]  # noqa: E712
        if not losses.empty:
            ax[1, 1].scatter(losses["rel_time"], losses["counter"],
                             color="orange", s=30, zorder=3, label="gap")
        if not crcbad.empty:
            ax[1, 1].scatter(crcbad["rel_time"], crcbad["counter"],
                             color="red", marker="x", s=40, zorder=3, label="CRC fail")
        ax[1, 1].set_title("Counter + fault markers")
        ax[1, 1].set_xlabel("Time (s)")
        ax[1, 1].set_ylabel("Rolling counter")
        ax[1, 1].legend()
        ax[1, 1].grid(True, alpha=0.3)

    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig(OUT_PATH, dpi=150)
    print(f"Report saved to {OUT_PATH}")

    # ---- Summary + bus-load estimate -------------------------------------
    total_frames = len(df)
    lost = int(sensors["gap"].fillna(0).sum()) if not sensors.empty else 0
    crc_fail = int((sensors["crc_ok"] == False).sum()) if not sensors.empty else 0  # noqa: E712
    bus_load = (total_frames * BITS_PER_FRAME) / (BITRATE * duration) * 100 if duration else 0

    print("\n--- Summary -------------------------------")
    print(f"Duration        : {duration:8.1f} s")
    print(f"Total frames    : {total_frames}")
    print(f"Sensor frames   : {len(sensors)}")
    print(f"Heartbeats      : {len(beats)}")
    print(f"Lost (gaps)     : {lost}")
    print(f"CRC failures    : {crc_fail}")
    print(f"Est. bus load   : {bus_load:.2f} %")


if __name__ == "__main__":
    main()