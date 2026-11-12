#!/usr/bin/env python3
"""Measure every field of a science sweep against the baseline and make the table and figure for the report.

Input: the output folder of the ensemble run (it holds fields/<name>.bin), and data/sweep/sweep.json
written by make_sweep.py (it knows each scenario's strategy, budget and seed).

Output (in --outdir, default results/):
    sweep_metrics.csv    one row per scenario: peak, mean, hot share, changes vs baseline, cooling per cell
    sweep_summary.csv    mean and standard deviation over the seeds, per strategy and budget
    sweep_cooling.png    the figure: change of mean temperature, of the hot share, and cooling per cell
                         against the budget, one line per strategy

Only strategies that use a seed (random, adjacent) have a spread; hottest and greenroof are single runs.

Usage:
    python3 tools/summarize_sweep.py --ens results/ens_28592 --sweep data/sweep/sweep.json
"""
import argparse
import csv
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scenario_metrics as sm  # noqa: E402

METRICS = ["peak", "mean", "p95", "hot_frac", "d_mean", "d_peak", "d_hot_frac", "changed_cells", "cooling_per_cell"]
STRATEGY_LABEL = {"random": "trees, random roads", "hottest": "trees, hottest roads",
                  "adjacent": "trees, next to green", "greenroof": "green roofs",
                  "scored": "trees, heat-relief score"}


def measure(ens_dir, sweep, threshold):
    """List of dict rows, one per scenario, plus the baseline row."""
    base_city = sm.read_city(sweep["baseline_city"])
    base = sm.read_field(os.path.join(ens_dir, "fields", "baseline.bin"))
    rows = []
    for sc in sweep["scenarios"]:
        fpath = os.path.join(ens_dir, "fields", sc["name"] + ".bin")
        if not os.path.exists(fpath):
            raise FileNotFoundError(f"missing field {fpath} (did the ensemble finish?)")
        res = sm.compare(sm.read_field(fpath), base, threshold, sm.read_city(sc["city"]), base_city)
        rows.append({"name": sc["name"], "strategy": sc["strategy"], "n": sc["n"],
                     "seed": "" if sc["seed"] is None else sc["seed"], **res})
    return rows, sm.field_stats(base, threshold)


def summarize(rows):
    """Mean and std (over seeds) per (strategy, n)."""
    groups = {}
    for r in rows:
        groups.setdefault((r["strategy"], r["n"]), []).append(r)
    out = []
    for (strategy, n), g in sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        row = {"strategy": strategy, "n": n, "runs": len(g)}
        for m in METRICS:
            v = np.array([x[m] for x in g], dtype=float)
            row[m + "_mean"] = float(v.mean())
            row[m + "_std"] = float(v.std(ddof=1)) if len(v) > 1 else 0.0
        out.append(row)
    return out


def write_csv(path, rows):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)


def make_figure(summary, path, threshold, base_stats):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    panels = [("d_mean", "Change of mean temperature (°C)", 1.0),
              ("d_hot_frac", f"Change of cells above {threshold:g} °C (percentage points)", 100.0),
              ("cooling_per_cell", "Cooling per converted cell (°C summed over the map)", 1.0)]
    colors = {"random": "#2a9d8f", "hottest": "#e76f51", "adjacent": "#6a8caf", "greenroof": "#9b8f3f", "scored": "#7b4fa3"}
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.2))
    for ax, (key, label, scale) in zip(axes, panels):
        for strat in ("random", "hottest", "adjacent", "greenroof", "scored"):
            pts = [s for s in summary if s["strategy"] == strat]
            if not pts:
                continue
            x = [p["n"] for p in pts]
            y = [p[key + "_mean"] * scale for p in pts]
            e = [p[key + "_std"] * scale for p in pts]
            ax.errorbar(x, y, yerr=e, marker="o", ms=4, lw=1.6, capsize=3, color=colors[strat],
                        label=STRATEGY_LABEL[strat])
        ax.set_xscale("log")
        ax.set_xlabel("Budget (cells converted)")
        ax.set_ylabel(label, fontsize=9)
        ax.axhline(0, color="#999", lw=0.8)
        ax.grid(alpha=0.25)
    axes[0].legend(fontsize=8, frameon=False)
    fig.suptitle(f"Sol-Retiro district, day-2 afternoon peak. Baseline: mean {base_stats['mean']:.2f} °C, "
                 f"{100 * base_stats['hot_frac']:.1f}% of cells above {threshold:g} °C. "
                 "Error bars: standard deviation over seeds.", fontsize=9)
    fig.tight_layout()
    fig.savefig(path, dpi=160)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ens", required=True, help="output folder of the science ensemble run")
    ap.add_argument("--sweep", default="data/sweep/sweep.json")
    ap.add_argument("--outdir", default="results")
    ap.add_argument("--threshold", type=float, default=sm.DEFAULT_THRESHOLD)
    ap.add_argument("--no-figure", action="store_true")
    a = ap.parse_args()

    with open(a.sweep) as f:
        sweep = json.load(f)
    try:
        rows, base_stats = measure(a.ens, sweep, a.threshold)
    except (OSError, ValueError) as exc:
        sys.exit(f"error: {exc}")
    summary = summarize(rows)
    write_csv(os.path.join(a.outdir, "sweep_metrics.csv"), rows)
    write_csv(os.path.join(a.outdir, "sweep_summary.csv"), summary)
    print(f"baseline: mean {base_stats['mean']:.3f} C, peak {base_stats['peak']:.3f} C, "
          f"{100 * base_stats['hot_frac']:.2f}% above {a.threshold:g} C")
    print(f"{'strategy':10s} {'cells':>6s} {'runs':>4s} {'d_mean (C)':>16s} {'d_hot (pts)':>14s} {'cooling/cell':>14s}")
    for s in summary:
        print(f"{s['strategy']:10s} {s['n']:6d} {s['runs']:4d} "
              f"{s['d_mean_mean']:+9.4f} ±{s['d_mean_std']:.4f} "
              f"{100 * s['d_hot_frac_mean']:+8.2f} ±{100 * s['d_hot_frac_std']:.2f} "
              f"{s['cooling_per_cell_mean']:8.2f} ±{s['cooling_per_cell_std']:.2f}")
    if not a.no_figure:
        path = os.path.join(a.outdir, "sweep_cooling.png")
        make_figure(summary, path, a.threshold, base_stats)
        print("figure", path)


if __name__ == "__main__":
    main()