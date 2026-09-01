#!/usr/bin/env python3
"""Summarise a 72-hour trial log from the pest trap.

Reads the CSV downloaded from the trap (http://<ip>/log.csv) and reports the
figures the study's success criteria are stated in:

  * minimum state of charge during night operation   (criterion: > 30 %)
  * telemetry delivery ratio                         (criterion: >= 95 %)
  * capture counts, by trigger source
  * logging completeness, so gaps are visible rather than silently averaged

Usage:
    python3 tools/analyze_trial.py trial-1-2026-09-01.csv
    python3 tools/analyze_trial.py trial-*.csv --csv summary.csv

No third-party dependencies: it runs on a stock Python 3.8+.
"""

import argparse
import csv
import glob
import statistics
import sys
from datetime import datetime, timedelta

NIGHT_STATES = {"LURE", "CAPTURE", "PURGE", "COOLDOWN"}
SOC_CRITERION = 30.0
DELIVERY_CRITERION = 95.0
LOG_INTERVAL_MIN = 15


def parse_time(stamp, uptime_s):
    """Rows carry an ISO timestamp, or 'uptime+N' when NTP never synced."""
    if stamp.startswith("uptime+"):
        return None, int(uptime_s or 0)
    try:
        return datetime.fromisoformat(stamp), int(uptime_s or 0)
    except ValueError:
        return None, int(uptime_s or 0)


def to_float(v):
    try:
        f = float(v)
    except (TypeError, ValueError):
        return None
    return None if f != f else f          # drop NaN (a flagged sensor gap)


def read_log(path):
    rows, events, malformed = [], [], 0
    with open(path, newline="") as fh:
        for rec in csv.reader(fh):
            if not rec or rec[0] == "iso_time":
                continue
            # Event rows are '<time>,EVENT,<type>,<detail>'.
            if len(rec) >= 3 and rec[1] == "EVENT":
                events.append({"time": rec[0], "type": rec[2],
                               "detail": rec[3] if len(rec) > 3 else ""})
                continue
            if len(rec) < 21:
                malformed += 1
                continue
            when, uptime = parse_time(rec[0], rec[1])
            rows.append({
                "time": when, "stamp": rec[0], "uptime": uptime,
                "state": rec[2],
                "vbat": to_float(rec[3]), "vocv": to_float(rec[4]),
                "soc": to_float(rec[5]), "vpv": to_float(rec[6]),
                "charging": rec[7] == "1",
                "temp": to_float(rec[8]), "rh": to_float(rec[9]),
                "ldr": to_float(rec[10]),
                "cat": to_float(rec[11]), "aph": to_float(rec[12]),
                "non": to_float(rec[13]), "conf": to_float(rec[14]),
                "captures": to_float(rec[15]), "fan_s": to_float(rec[16]),
                "cam_state": to_float(rec[17]), "rssi": to_float(rec[18]),
                "pub_ok": to_float(rec[19]), "pub_fail": to_float(rec[20]),
            })
    return rows, events, malformed


def is_night(row):
    """Night by operating state, falling back to the clock when idle."""
    if row["state"] in NIGHT_STATES or row["state"] == "LOW_BATTERY":
        return True
    if row["time"] is not None:
        return row["time"].hour >= 18 or row["time"].hour < 5
    return False


def night_key(row):
    """Groups a night by the calendar date it started on."""
    if row["time"] is None:
        return None
    t = row["time"]
    return (t - timedelta(hours=12)).date()


def find_gaps(rows):
    gaps = []
    tolerance = LOG_INTERVAL_MIN * 60 * 1.5
    for a, b in zip(rows, rows[1:]):
        if a["time"] and b["time"]:
            delta = (b["time"] - a["time"]).total_seconds()
        else:
            delta = b["uptime"] - a["uptime"]
            if delta < 0:                 # uptime went backwards: a reset
                gaps.append((a["stamp"], b["stamp"], -1))
                continue
        if delta > tolerance:
            gaps.append((a["stamp"], b["stamp"], delta / 60.0))
    return gaps


def analyse(path):
    rows, events, malformed = read_log(path)
    if not rows:
        return None

    first, last = rows[0], rows[-1]
    if first["time"] and last["time"]:
        duration_h = (last["time"] - first["time"]).total_seconds() / 3600.0
    else:
        duration_h = (last["uptime"] - first["uptime"]) / 3600.0

    socs = [r["soc"] for r in rows if r["soc"] is not None]
    night_rows = [r for r in rows if is_night(r) and r["soc"] is not None]

    per_night = {}
    for r in night_rows:
        k = night_key(r)
        per_night.setdefault(k, []).append(r["soc"])

    published = last["pub_ok"] or 0.0
    failed = last["pub_fail"] or 0.0
    generated = published + failed
    delivery = (100.0 * published / generated) if generated else 100.0

    temps = [r["temp"] for r in rows if r["temp"] is not None]
    rhs = [r["rh"] for r in rows if r["rh"] is not None]

    ev_counts = {}
    for e in events:
        ev_counts[e["type"]] = ev_counts.get(e["type"], 0) + 1

    expected = int(duration_h * 60 / LOG_INTERVAL_MIN) if duration_h > 0 else 0

    return {
        "path": path,
        "rows": len(rows), "malformed": malformed,
        "duration_h": duration_h,
        "expected_rows": expected,
        "completeness": (100.0 * len(rows) / expected) if expected else 100.0,
        "gaps": find_gaps(rows),
        "soc_start": socs[0] if socs else None,
        "soc_end": socs[-1] if socs else None,
        "soc_min": min(socs) if socs else None,
        "soc_min_night": min(r["soc"] for r in night_rows) if night_rows else None,
        "per_night": {k: min(v) for k, v in per_night.items()},
        "published": int(published), "generated": int(generated),
        "delivery": delivery,
        "captures": int(last["captures"] or 0),
        "fan_s": int(last["fan_s"] or 0),
        "events": ev_counts,
        "det_cat": sum(r["cat"] or 0 for r in rows),
        "det_aph": sum(r["aph"] or 0 for r in rows),
        "det_non": sum(r["non"] or 0 for r in rows),
        "temp": (min(temps), statistics.mean(temps), max(temps)) if temps else None,
        "rh": (min(rhs), statistics.mean(rhs), max(rhs)) if rhs else None,
        "env_gaps": sum(1 for r in rows if r["temp"] is None),
    }


def report(a):
    print("=" * 68)
    print(f"Trial log: {a['path']}")
    print("=" * 68)
    print(f"  Duration          {a['duration_h']:.1f} h "
          f"({a['duration_h'] / 24:.2f} days)")
    print(f"  Records           {a['rows']} of ~{a['expected_rows']} expected "
          f"({a['completeness']:.1f} % complete)")
    if a["malformed"]:
        print(f"  Malformed rows    {a['malformed']}")

    print("\n-- Power autonomy " + "-" * 49)
    if a["soc_start"] is not None:
        print(f"  SoC start / end   {a['soc_start']:.1f} %  ->  {a['soc_end']:.1f} %")
        print(f"  SoC minimum       {a['soc_min']:.1f} % (whole trial)")
    if a["soc_min_night"] is not None:
        ok = a["soc_min_night"] > SOC_CRITERION
        print(f"  SoC min at night  {a['soc_min_night']:.1f} %   "
              f"[{'PASS' if ok else 'FAIL'}: criterion > {SOC_CRITERION:.0f} %]")
    for night, low in sorted((k, v) for k, v in a["per_night"].items() if k):
        flag = "" if low > SOC_CRITERION else "   <-- below threshold"
        print(f"    night of {night}   min {low:.1f} %{flag}")
    print(f"  Blower run time   {a['fan_s']} s total")

    print("\n-- Data integrity " + "-" * 49)
    ok = a["delivery"] >= DELIVERY_CRITERION
    print(f"  Delivered         {a['published']} of {a['generated']} records")
    print(f"  Delivery ratio    {a['delivery']:.1f} %   "
          f"[{'PASS' if ok else 'FAIL'}: criterion >= {DELIVERY_CRITERION:.0f} %]")
    if a["gaps"]:
        print(f"  Logging gaps      {len(a['gaps'])}")
        for start, end, mins in a["gaps"][:5]:
            if mins < 0:
                print(f"    controller reset between {start} and {end}")
            else:
                print(f"    {mins:.0f} min gap: {start} -> {end}")
        if len(a["gaps"]) > 5:
            print(f"    ... and {len(a['gaps']) - 5} more")
    else:
        print("  Logging gaps      none")

    print("\n-- Trap activity " + "-" * 50)
    print(f"  Capture cycles    {a['captures']}")
    for name in sorted(a["events"]):
        print(f"    {name:<18}{a['events'][name]}")
    print(f"  Detections logged cat={a['det_cat']:.0f} "
          f"aph={a['det_aph']:.0f} non-target={a['det_non']:.0f}")
    print("    (per-sample snapshots, not a total insect count -- the")
    print("     chamber audit is the count that goes in the results)")

    print("\n-- Environment " + "-" * 52)
    if a["temp"]:
        print(f"  Temperature       {a['temp'][0]:.1f} / {a['temp'][1]:.1f} / "
              f"{a['temp'][2]:.1f} C  (min/mean/max)")
    if a["rh"]:
        print(f"  Humidity          {a['rh'][0]:.1f} / {a['rh'][1]:.1f} / "
              f"{a['rh'][2]:.1f} %  (min/mean/max)")
    if a["env_gaps"]:
        print(f"  DHT22 read gaps   {a['env_gaps']} rows")
    print()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+", help="trial CSV file(s); globs accepted")
    ap.add_argument("--csv", help="also write a one-row-per-trial summary here")
    args = ap.parse_args()

    paths = []
    for pattern in args.logs:
        paths.extend(sorted(glob.glob(pattern)) or [pattern])

    results = []
    for p in paths:
        try:
            a = analyse(p)
        except OSError as exc:
            print(f"cannot read {p}: {exc}", file=sys.stderr)
            continue
        if a is None:
            print(f"{p}: no data rows", file=sys.stderr)
            continue
        results.append(a)
        report(a)

    if len(results) > 1:
        print("=" * 68)
        print("All trials")
        print("=" * 68)
        print(f"{'trial':<28}{'hours':>7}{'min night SoC':>15}{'delivery':>11}"
              f"{'captures':>10}")
        for a in results:
            soc = f"{a['soc_min_night']:.1f} %" if a["soc_min_night"] is not None else "-"
            print(f"{a['path'][:27]:<28}{a['duration_h']:>7.1f}{soc:>15}"
                  f"{a['delivery']:>10.1f} %{a['captures']:>10}")
        print()

    if args.csv and results:
        with open(args.csv, "w", newline="") as fh:
            w = csv.writer(fh)
            w.writerow(["trial", "hours", "records", "completeness_pct",
                        "soc_start", "soc_end", "soc_min_night",
                        "delivery_pct", "captures", "fan_seconds"])
            for a in results:
                w.writerow([a["path"], f"{a['duration_h']:.2f}", a["rows"],
                            f"{a['completeness']:.1f}", a["soc_start"],
                            a["soc_end"], a["soc_min_night"],
                            f"{a['delivery']:.2f}", a["captures"], a["fan_s"]])
        print(f"summary written to {args.csv}")

    # Non-zero exit if any trial missed a criterion, so this can gate a script.
    failed = [a for a in results
              if (a["soc_min_night"] is not None and a["soc_min_night"] <= SOC_CRITERION)
              or a["delivery"] < DELIVERY_CRITERION]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
