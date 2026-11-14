#!/usr/bin/env python3
"""Turn results/ensemble.csv (written by run_ensemble) into the benchmark figures and tables.

Runs are named  bench_<list>_<mode>_w<workers>_n<nodes>_r<repeat>  (see slurm/ens_bench_submit.sh).
Repeats of the same configuration are combined with the MEDIAN; error bars show min and max.

Output (in --outdir, default results/):
    bench_summary.csv      one row per configuration: median/min/max wall time, efficiency, imbalance
    bench_scheduling.png   static / cyclic / dynamic at W=31, three job lists
    bench_scaling.png      speed-up and efficiency against the number of workers (one node, dynamic)
    bench_nodes.png        31 workers on one node against 31 workers on two nodes

Usage:
    python3 tools/summarize_bench.py --csv results/ensemble.csv
"""
import argparse
import csv
import os
import re
import sys

import numpy as np

TAG = re.compile(r"^bench_(skewed_sorted|skewed|uniform)_(static|cyclic|dynamic)_w(\d+)_n(\d+)_r(\d+)$")
LISTS = ["uniform", "skewed", "skewed_sorted"]
LIST_LABEL = {"uniform": "uniform costs", "skewed": "skewed, random order", "skewed_sorted": "skewed, heaviest first"}
MODES = ["static", "cyclic", "dynamic"]
MODE_COLOR = {"static": "#c9503c", "cyclic": "#d9a441", "dynamic": "#2a9d8f"}
NUM = ["workers", "nodes", "jobs", "wall_s", "sum_job_s", "speedup", "efficiency", "busy_min_s", "busy_max_s", "imbalance", "failed"]


def parse_tag(tag):
    """(list, mode, workers, nodes, repeat) or None if the tag is not a benchmark run."""
    m = TAG.match(tag)
    if not m:
        return None
    return m.group(1), m.group(2), int(m.group(3)), int(m.group(4)), int(m.group(5))


def read_runs(path):
    runs = []
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            p = parse_tag(row["tag"])
            if p is None:
                continue
            r = {"list": p[0], "mode": p[1], "w": p[2], "nodes": p[3], "rep": p[4]}
            for k in NUM:
                r[k] = float(row[k])
            runs.append(r)
    return runs


def group(runs):
    """Median/min/max over repeats per (list, mode, workers, nodes). Runs with failures are dropped."""
    g = {}
    for r in runs:
        if r["failed"] == 0:
            g.setdefault((r["list"], r["mode"], r["w"], r["nodes"]), []).append(r)
    out = {}
    for key, rs in g.items():
        walls = np.array([x["wall_s"] for x in rs])
        out[key] = {"reps": len(rs), "wall": float(np.median(walls)), "wall_min": float(walls.min()),
                    "wall_max": float(walls.max()),
                    "sum_job": float(np.median([x["sum_job_s"] for x in rs])),
                    "efficiency": float(np.median([x["efficiency"] for x in rs])),
                    "imbalance": float(np.median([x["imbalance"] for x in rs]))}
    return out


def write_summary(path, groups):
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["list", "mode", "workers", "nodes", "reps", "wall_median_s", "wall_min_s", "wall_max_s",
                    "sum_job_median_s", "efficiency_median", "imbalance_median"])
        for (lst, mode, wk, nodes), v in sorted(groups.items()):
            w.writerow([lst, mode, wk, nodes, v["reps"], f"{v['wall']:.3f}", f"{v['wall_min']:.3f}",
                        f"{v['wall_max']:.3f}", f"{v['sum_job']:.3f}", f"{v['efficiency']:.4f}", f"{v['imbalance']:.4f}"])


def _err(v):
    return [[v["wall"] - v["wall_min"]], [v["wall_max"] - v["wall"]]]


def fig_scheduling(groups, path, w):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(8, 4.4))
    width = 0.26
    for i, mode in enumerate(MODES):
        for j, lst in enumerate(LISTS):
            v = groups.get((lst, mode, w, 1))
            if not v:
                continue
            x = j + (i - 1) * width
            ax.bar(x, v["wall"], width * 0.92, color=MODE_COLOR[mode], label=mode if j == 0 else None)
            ax.errorbar(x, v["wall"], yerr=_err(v), color="#333", capsize=3, lw=1)
            ax.text(x, v["wall_max"] * 1.02, f"{v['wall']:.1f}", ha="center", va="bottom", fontsize=8)
    ax.set_xticks(range(len(LISTS)))
    ax.set_xticklabels([LIST_LABEL[l] for l in LISTS])
    ax.set_ylabel("Wall time for 256 jobs (s), lower is better")
    ax.set_title(f"Scheduling of the ensemble, {w} workers on one node", fontsize=10)
    ax.legend(frameon=False)
    ax.grid(axis="y", alpha=0.25)
    fig.tight_layout()
    fig.savefig(path, dpi=160)


def fig_scaling(groups, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, (a1, a2) = plt.subplots(1, 2, figsize=(10, 4.2))
    style = {"uniform": ("#2a9d8f", "o"), "skewed": ("#e76f51", "s")}
    maxw = 1
    for lst, (col, mk) in style.items():
        pts = sorted((k[2], v) for k, v in groups.items() if k[0] == lst and k[1] == "dynamic" and k[3] == 1)
        if len(pts) < 2:
            continue
        ws = [p[0] for p in pts]
        maxw = max(maxw, max(ws))
        base = pts[0][1]["wall"] * pts[0][0]                 # wall x workers of the smallest run = serial work estimate
        sp = [base / p[1]["wall"] for p in pts]
        a1.plot(ws, sp, marker=mk, color=col, label=LIST_LABEL[lst])
        a2.plot(ws, [s / w for s, w in zip(sp, ws)], marker=mk, color=col, label=LIST_LABEL[lst])
    a1.plot([1, maxw], [1, maxw], "--", color="#888", label="ideal")
    a1.set_xlabel("Workers")
    a1.set_ylabel("Speed-up (relative to the smallest run)")
    a2.axhline(1.0, ls="--", color="#888")
    a2.set_xlabel("Workers")
    a2.set_ylabel("Efficiency = speed-up / workers\n(relative to the smallest run)")
    a2.set_ylim(0, 1.1)
    ticks = sorted({k[2] for k in groups if k[1] == "dynamic" and k[3] == 1})
    for ax in (a1, a2):
        ax.set_xscale("log", base=2)
        ax.set_xticks(ticks)
        ax.set_xticklabels([str(t) for t in ticks])
        ax.minorticks_off()
        ax.grid(alpha=0.25)
    a1.set_yscale("log", base=2)
    a1.legend(frameon=False, fontsize=8)
    fig.suptitle("Ensemble scaling, dynamic scheduling, one node", fontsize=10)
    fig.tight_layout()
    fig.savefig(path, dpi=160)


def fig_nodes(groups, path, w):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, ax = plt.subplots(figsize=(6, 4))
    width = 0.34
    for i, nodes in enumerate((1, 2)):
        for j, lst in enumerate(["uniform", "skewed"]):
            v = groups.get((lst, "dynamic", w, nodes))
            if not v:
                continue
            x = j + (i - 0.5) * width
            ax.bar(x, v["wall"], width * 0.92, color=["#6a8caf", "#2a9d8f"][i], label=f"{nodes} node{'s' if nodes > 1 else ''}" if j == 0 else None)
            ax.errorbar(x, v["wall"], yerr=_err(v), color="#333", capsize=3, lw=1)
            ax.text(x, v["wall_max"] * 1.02, f"{v['wall']:.1f}", ha="center", va="bottom", fontsize=8)
    ax.set_xticks([0, 1])
    ax.set_xticklabels(["uniform costs", "skewed, random order"])
    ax.set_ylabel("Wall time for 256 jobs (s)")
    ax.set_title(f"{w} workers: one node against two nodes", fontsize=10)
    ax.legend(frameon=False)
    ax.grid(axis="y", alpha=0.25)
    fig.tight_layout()
    fig.savefig(path, dpi=160)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", default="results/ensemble.csv")
    ap.add_argument("--outdir", default="results")
    ap.add_argument("--workers", type=int, default=31, help="worker count of the scheduling and node comparisons")
    ap.add_argument("--no-figures", action="store_true")
    a = ap.parse_args()

    runs = read_runs(a.csv)
    if not runs:
        sys.exit("error: no benchmark runs (tags bench_*) found in " + a.csv)
    groups = group(runs)
    os.makedirs(a.outdir, exist_ok=True)
    write_summary(os.path.join(a.outdir, "bench_summary.csv"), groups)
    reps = sorted({v["reps"] for v in groups.values()})
    print(f"{len(runs)} runs, {len(groups)} configurations, repeats per configuration: {reps}")
    print(f"\nscheduling, {a.workers} workers, 1 node (median wall s, efficiency):")
    for lst in LISTS:
        cells = []
        for mode in MODES:
            v = groups.get((lst, mode, a.workers, 1))
            cells.append(f"{mode} {v['wall']:6.2f} s ({v['efficiency']:.2f})" if v else f"{mode} n/a")
        print(f"  {LIST_LABEL[lst]:24s} " + "   ".join(cells))
    print("\nscaling, dynamic, 1 node (median wall s, efficiency):")
    for lst in ("uniform", "skewed"):
        pts = sorted((k[2], v) for k, v in groups.items() if k[0] == lst and k[1] == "dynamic" and k[3] == 1)
        print(f"  {lst:8s} " + "  ".join(f"W={w}: {v['wall']:.1f} s ({v['efficiency']:.2f})" for w, v in pts))
    if not a.no_figures:
        fig_scheduling(groups, os.path.join(a.outdir, "bench_scheduling.png"), a.workers)
        fig_scaling(groups, os.path.join(a.outdir, "bench_scaling.png"))
        fig_nodes(groups, os.path.join(a.outdir, "bench_nodes.png"), a.workers)
        print("\nfigures: bench_scheduling.png, bench_scaling.png, bench_nodes.png in", a.outdir)


if __name__ == "__main__":
    main()
