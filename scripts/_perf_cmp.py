#!/usr/bin/env python3
"""Compare two _perf_baseline.sh CSVs: total scored time and per-case deltas.

Usage: _perf_cmp.py before.csv after.csv [min_pct]
Only prints cases whose scored time moved by more than min_pct (default 3%).
"""
import csv
import sys


def load(path):
    out = {}
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            try:
                out[row["case"]] = (row["status"], int(row["timed_us"]))
            except (ValueError, KeyError):
                out[row["case"]] = (row.get("status", "?"), None)
    return out


def main():
    before, after = load(sys.argv[1]), load(sys.argv[2])
    min_pct = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0

    tb = sum(v[1] for v in before.values() if v[1])
    ta = sum(v[1] for v in after.values() if v[1])
    print(f"total scored: before={tb/1e6:.3f}s after={ta/1e6:.3f}s "
          f"delta={(ta-tb)/tb*100:+.2f}%")

    for name in sorted(set(before) | set(after)):
        sb, vb = before.get(name, ("MISSING", None))
        sa, va = after.get(name, ("MISSING", None))
        if sb != sa:
            print(f"  STATUS {name}: {sb} -> {sa}")
            continue
        if not vb or not va:
            continue
        pct = (va - vb) / vb * 100
        if abs(pct) >= min_pct:
            print(f"  {name}: {vb} -> {va} us ({pct:+.1f}%)")


main()
