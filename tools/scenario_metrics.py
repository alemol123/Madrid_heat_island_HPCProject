#!/usr/bin/env python3
"""Measure a temperature field and compare it with a baseline.

Field file (docs/contract.md): int32 nx, ny (little-endian), then nx*ny float32, row-major.
This is what `heat_serial --out` writes.

Numbers printed:
    peak      hottest cell (deg C)
    mean      average over all cells
    p95       95th percentile
    hot_frac  share of cells hotter than --threshold (default 29.0 deg C, see docs)
With --baseline (a field from the no-change city) it also prints the change:
    d_mean, d_peak, d_hot_frac    scenario minus baseline (negative = cooler)
With --city and --baseline-city as well, it adds the cooling efficiency:
    changed_cells   how many cells the scenario converted
    cooling_per_cell  sum of (baseline - scenario) over ALL cells / changed_cells
                      = degrees of cooling bought per converted cell, summed over the map.
                      This is the number to use when comparing strategies at the same budget.

Usage:
    python3 tools/scenario_metrics.py --field out/s.bin
    python3 tools/scenario_metrics.py --field out/s.bin --baseline out/base.bin \\
        --city data/s.city --baseline-city data/district.city --tag trees_hottest_500 --csv results/scenarios.csv
"""
import argparse
import csv
import os
import sys

import numpy as np

DEFAULT_THRESHOLD = 29.0       # deg C; 30 never triggers with the frozen coefficients


def read_field(path):
    with open(path, "rb") as f:
        head = np.fromfile(f, dtype="<i4", count=2)
        if head.size != 2:
            raise ValueError(f"{path}: file too short for a header")
        nx, ny = int(head[0]), int(head[1])
        data = np.fromfile(f, dtype="<f4", count=nx * ny)
    if data.size != nx * ny:
        raise ValueError(f"{path}: expected {nx * ny} values, found {data.size}")
    return data.reshape(ny, nx)


def write_field(path, field):
    ny, nx = field.shape
    with open(path, "wb") as f:
        np.array([nx, ny], dtype="<i4").tofile(f)
        np.ascontiguousarray(field, dtype="<f4").tofile(f)


def read_city(path):
    with open(path, "rb") as f:
        head = np.fromfile(f, dtype="<i4", count=2)
        nx, ny = int(head[0]), int(head[1])
        city = np.fromfile(f, dtype=np.int8, count=nx * ny)
    return city.reshape(ny, nx)


def field_stats(field, threshold=DEFAULT_THRESHOLD):
    f = field.astype(np.float64)
    return {"peak": float(f.max()), "mean": float(f.mean()),
            "p95": float(np.percentile(f, 95)),
            "hot_frac": float((f > threshold).mean())}


def compare(field, baseline, threshold=DEFAULT_THRESHOLD, city=None, baseline_city=None):
    if not np.isfinite(field).all():
        raise ValueError("field contains NaN or inf: the run probably blew up")
    out = field_stats(field, threshold)
    if baseline is not None:
        if baseline.shape != field.shape:
            raise ValueError(f"shape mismatch: field {field.shape} vs baseline {baseline.shape}")
        b = field_stats(baseline, threshold)
        out["d_mean"] = out["mean"] - b["mean"]
        out["d_peak"] = out["peak"] - b["peak"]
        out["d_hot_frac"] = out["hot_frac"] - b["hot_frac"]
        if city is not None and baseline_city is not None:
            if city.shape != field.shape or baseline_city.shape != field.shape:
                raise ValueError("city and field shapes differ")
            changed = int((city != baseline_city).sum())
            out["changed_cells"] = changed
            drop = float((baseline.astype(np.float64) - field.astype(np.float64)).sum())
            out["cooling_per_cell"] = drop / changed if changed else 0.0
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--field", required=True)
    ap.add_argument("--baseline", help="field file of the unchanged city")
    ap.add_argument("--city", help="city file used for --field")
    ap.add_argument("--baseline-city", help="city file used for --baseline")
    ap.add_argument("--threshold", type=float, default=DEFAULT_THRESHOLD)
    ap.add_argument("--tag", default="", help="label written in the CSV row")
    ap.add_argument("--csv", help="append one row to this CSV (header written if new)")
    a = ap.parse_args()

    field = read_field(a.field)
    base = read_field(a.baseline) if a.baseline else None
    city = read_city(a.city) if a.city else None
    bcity = read_city(a.baseline_city) if a.baseline_city else None
    try:
        res = compare(field, base, a.threshold, city, bcity)
    except ValueError as exc:
        sys.exit(f"error: {exc}")

    print(f"{a.field}: {field.shape[1]}x{field.shape[0]} cells, threshold {a.threshold} C")
    for k, v in res.items():
        print(f"  {k:17s} {v:+.4f}" if k.startswith(("d_", "cooling")) else f"  {k:17s} {v:.4f}")

    if a.csv:
        row = {"tag": a.tag, "field": os.path.basename(a.field), "threshold": a.threshold, **res}
        os.makedirs(os.path.dirname(os.path.abspath(a.csv)), exist_ok=True)
        new = not os.path.exists(a.csv)
        if new:
            names = list(row)
        else:                                   # keep the columns of the existing file
            with open(a.csv, newline="") as f:
                names = next(csv.reader(f))
        with open(a.csv, "a", newline="") as f:
            w = csv.DictWriter(f, fieldnames=names, restval="", extrasaction="ignore")
            if new:
                w.writeheader()
            w.writerow(row)
        print("appended to", a.csv)


if __name__ == "__main__":
    main()
