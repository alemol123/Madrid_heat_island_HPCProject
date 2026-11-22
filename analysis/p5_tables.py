#!/usr/bin/env python3
"""P5 result tables (and one plot) for the report, all computed from files in the repo.

  python3 analysis/p5_tables.py cpu-vs-gpu   # results/plots/p5_cpu_vs_gpu.{md,png}
  python3 analysis/p5_tables.py l2           # results/plots/p5_l2_test.md
  python3 analysis/p5_tables.py spread       # results/plots/p5_l3_spread.md
  python3 analysis/p5_tables.py all

cpu-vs-gpu  Same work per grid size (P1's size-sweep sizes and steps, ~4e9 cell updates):
            CPU 1 core (AVX-512, MPI 1 rank) and CPU 32 cores (MPI 32 ranks, our account limit),
            both tag cpu32_bind on fortran; GPU resident (gpu_sizes_r2) and GPU --bc-once
            (gpu_sizes_bconce) on the RTX 6000 Ada. Two metrics:
              loop        = the time-stepping loop. GPU: time_compute (copies excluded).
                            CPU: time_total - time_io (loop + a few ms of allocation/initialisation;
                            compute+comm is not used because MPI reports the maximum of each
                            separately, so their sum overestimates).
              end-to-end  = time_total for both (GPU includes start-up ~0.13 s and the copies).
l2          step_rows kernel time (nsys) naive vs resident: 4096² plain, 4096² with --flush-l2 256,
            8192². If the naive advantage comes from L2, it must vanish with the flush.
spread      Same allocation (4 chiplets), same ranks: Slurm's default placement (2 chiplets)
            vs spread over all 4 chiplets (slurm/l3_spread_test.sh).

Medians of the repeats. Needs pandas and matplotlib (env/requirements-analysis.txt).
"""
import argparse
import re
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

BLUE, INK, INK2, GRID, SURF = "#2a78d6", "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"
SERIES = [BLUE, "#e07b39", "#3a9a5b", "#7a5cc4"]
plt.rcParams.update({"font.size": 10, "axes.edgecolor": INK2, "axes.labelcolor": INK, "text.color": INK,
                     "xtick.color": INK2, "ytick.color": INK2, "axes.spines.top": False,
                     "axes.spines.right": False, "figure.facecolor": SURF, "axes.facecolor": SURF,
                     "savefig.facecolor": SURF})

# nsys jobs of the L2 test: (grid, condition, mode) -> Slurm job id
L2_JOBS = [(4096, "normal", "resident", 28697), (4096, "normal", "naive", 28698),
           (4096, "L2 flushed every step", "naive", 31361), (4096, "L2 flushed every step", "resident", 31362),
           (8192, "normal", "naive", 31363), (8192, "normal", "resident", 31364)]
SPREAD_JOBS = [32611, 32612]


def write(path, lines):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    print(f"\nwrote {path}")


def cpu_vs_gpu(d, outdir):
    def pick(tag, ranks=None):
        s = d[d.tag == tag]
        if ranks is not None:
            s = s[s.ranks == ranks]
        return s
    variants = [("CPU, 1 core (AVX-512)", pick("cpu32_bind", 1), "cpu"),
                ("CPU, 32 cores (MPI)", pick("cpu32_bind", 32), "cpu"),
                ("GPU, resident", pick("gpu_sizes_r2"), "gpu"),
                ("GPU, resident + --bc-once", pick("gpu_sizes_bconce"), "gpu")]
    sizes = sorted(set.intersection(*[set(v[1].nx) for v in variants]))
    if not sizes:
        sys.exit("no grid size is present in all four variants")
    rows = []
    for n in sizes:
        steps = {int(s) for _, v, _ in variants for s in v[v.nx == n].steps}
        if len(steps) != 1:
            sys.exit(f"{n}²: variants use different step counts {sorted(steps)}; not the same work")
        st = steps.pop()
        for name, v, kind in variants:
            g = v[v.nx == n]
            loop = g.time_compute if kind == "gpu" else g.time_total - g.time_io
            rows.append(dict(n=n, steps=st, name=name, reps=len(g), loop=loop.median(),
                             total=g.time_total.median(),
                             ns=1e9 * loop.median() / (n * n * st), node=",".join(sorted(set(g.node)))))
    r = pd.DataFrame(rows)
    lines = ["| Grid (steps) | Variant | Repeats | Loop (s) | ns per cell update | End-to-end (s) | "
             "Loop speed-up vs CPU 32 cores | Node |", "|---|---|---|---|---|---|---|---|"]
    for n in sizes:
        g = r[r.n == n]
        ref = g[g.name == "CPU, 32 cores (MPI)"].loop.iloc[0]
        for x in g.itertuples():
            lines.append(f"| {n}² ({x.steps}) | {x.name} | {x.reps} | {x.loop:.4f} | {x.ns:.4f} | "
                         f"{x.total:.3f} | {ref / x.loop:.2f}× | {x.node} |")
    lines += ["", "Same work in every row of a grid size. Loop: GPU = time_compute (copies excluded); "
              "CPU = time_total - time_io. End-to-end = time_total (GPU includes start-up and copies). "
              "Medians of the repeats; 32 cores is the account limit (64 CPUs counted as 2 threads per core)."]
    write(outdir / "p5_cpu_vs_gpu.md", lines)

    fig, ax = plt.subplots(figsize=(7, 3.8))
    w = 0.2
    for i, (name, _, _) in enumerate(variants):
        g = r[r.name == name]
        xs = [sizes.index(n) + (i - 1.5) * w for n in g.n]
        ax.bar(xs, g.ns, width=w, color=SERIES[i], label=name)
    ax.set_yscale("log")
    ax.set_xticks(range(len(sizes)))
    ax.set_xticklabels([f"{n}²" for n in sizes])
    ax.set_xlabel("Grid size (same work per size, ~4e9 cell updates)")
    ax.set_ylabel("Loop time per cell update (ns, log)")
    ax.set_title("CPU vs GPU, same work", fontsize=10, loc="left")
    ax.grid(axis="y", color=GRID, lw=0.8)
    ax.set_axisbelow(True)
    ax.legend(frameon=False, fontsize=8, loc="upper center", bbox_to_anchor=(0.5, -0.2), ncol=2)
    fig.tight_layout()
    fig.savefig(outdir / "p5_cpu_vs_gpu.png", dpi=200, bbox_inches="tight")
    print(f"wrote {outdir / 'p5_cpu_vs_gpu.png'}")


def kernel_us(path):
    for line in open(path):
        if "step_rows" in line:
            p = line.split()
            return float(p[3]) / 1000.0, int(p[2])   # Avg (ns) -> us, instances
    sys.exit(f"no step_rows line in {path}")


def l2(profiles, outdir):
    t = {}
    for n, cond, mode, job in L2_JOBS:
        files = list(Path(profiles).glob(f"nsys_{mode}_{n}_{job}_stats.txt"))
        if len(files) != 1:
            sys.exit(f"profile summary for job {job} not found in {profiles}")
        t[(n, cond, mode)] = kernel_us(files[0]) + (job,)
    lines = ["| Grid | Condition | naive (µs) | resident (µs) | naive faster by | Jobs |", "|---|---|---|---|---|---|"]
    for n, cond in [(4096, "normal"), (4096, "L2 flushed every step"), (8192, "normal")]:
        na, re_ = t[(n, cond, "naive")], t[(n, cond, "resident")]
        lines.append(f"| {n}² | {cond} | {na[0]:.1f} | {re_[0]:.1f} | {100 * (1 - na[0] / re_[0]):.1f}% | "
                     f"{na[2]}, {re_[2]} |")
    lines += ["", "Average GPU time of the step_rows kernel per step (Nsight Systems, cuda_gpu_kern_sum, "
              "200 steps). --flush-l2 256 writes 256 MB before every step, evicting the grid from the "
              "96 MB L2 cache of the RTX 6000 Ada."]
    write(outdir / "p5_l2_test.md", lines)


def spread(d, outdir):
    s = d[d.tag.str.startswith("l3spread_") & d.slurm_jobid.astype(str).isin([str(j) for j in SPREAD_JOBS])]
    if s.empty:
        sys.exit("no l3spread rows for jobs " + ", ".join(map(str, SPREAD_JOBS)))
    lines = ["| Grid | Ranks | Default: 2 chiplets (s) | Spread: 4 chiplets (s) | Spread faster | Repeats |",
             "|---|---|---|---|---|---|"]
    for (n, r), g in s.groupby(["nx", "ranks"]):
        de = g[g.tag == "l3spread_default"].time_compute
        sp = g[g.tag == "l3spread_spread"].time_compute
        lines.append(f"| {n}² | {r} | {de.median():.3f} | {sp.median():.3f} | {de.median() / sp.median():.2f}× | "
                     f"{len(de)} + {len(sp)} |")
    lines += ["", "Compute time, medians. Same allocation (32 cores on 4 chiplets of fortran, AMD EPYC 9755, "
              "32 MB L3 per 8-core chiplet), placements alternated in every repeat. Jobs "
              + ", ".join(map(str, SPREAD_JOBS)) + "."]
    write(outdir / "p5_l3_spread.md", lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("what", choices=["cpu-vs-gpu", "l2", "spread", "all"])
    ap.add_argument("--csv", default="results/runs.csv")
    ap.add_argument("--profiles", default="results/profiles")
    ap.add_argument("--outdir", default="results/plots", type=Path)
    a = ap.parse_args()
    d = pd.read_csv(a.csv)
    d["node"] = d["node"].fillna("?").astype(str)
    if a.what in ("cpu-vs-gpu", "all"):
        cpu_vs_gpu(d, a.outdir)
    if a.what in ("l2", "all"):
        l2(a.profiles, a.outdir)
    if a.what in ("spread", "all"):
        spread(d, a.outdir)


if __name__ == "__main__":
    main()
