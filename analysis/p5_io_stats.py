#!/usr/bin/env python3
"""Gather vs MPI-IO output time, paired statistics (P5).

slurm/io_mpi.sh runs the two methods alternately (gather, mpiio, gather, mpiio, ...), so
repeat i of gather and repeat i of mpiio ran seconds apart and saw the same file-server
load. This script compares them pair by pair, which cancels the slow drift of the shared
NFS server.

For every configuration (grid, nodes, ranks) of one Slurm job it reports:
  - median time_io of each method
  - paired ratio mpiio/gather: median of the per-pair ratios, with a bootstrap 95% range
  - how many pairs MPI-IO was faster in
  - Wilcoxon signed-rank test on log(ratio) (two-sided, normal approximation), and the
    Holm-adjusted p-value, because several configurations are tested at once

Usage (repo root):
  python3 analysis/p5_io_stats.py --tag ioalt --job 31409 [--out results/plots/p5_io_paired.md]

Standard library only (runs on the cluster's system python3 too).
"""
import argparse
import csv
import math
import random
import statistics as st
import sys
from pathlib import Path


def wilcoxon_p(diffs):
    """Two-sided p-value of the Wilcoxon signed-rank test (normal approximation,
    tie-corrected). diffs: paired differences; zeros are dropped."""
    d = [x for x in diffs if x != 0.0]
    n = len(d)
    if n == 0:
        return 1.0
    order = sorted(range(n), key=lambda i: abs(d[i]))
    ranks = [0.0] * n
    i = 0
    ties = 0.0
    while i < n:                      # average ranks for ties
        j = i
        while j + 1 < n and abs(d[order[j + 1]]) == abs(d[order[i]]):
            j += 1
        r = (i + j) / 2 + 1
        for k in range(i, j + 1):
            ranks[order[k]] = r
        t = j - i + 1
        ties += t ** 3 - t
        i = j + 1
    w_plus = sum(r for r, x in zip(ranks, d) if x > 0)
    mean = n * (n + 1) / 4
    var = n * (n + 1) * (2 * n + 1) / 24 - ties / 48
    if var <= 0:
        return 1.0
    z = (w_plus - mean) / math.sqrt(var)
    return math.erfc(abs(z) / math.sqrt(2))


def holm(ps):
    """Holm step-down adjustment of a list of p-values (same order as given)."""
    m = len(ps)
    idx = sorted(range(m), key=lambda i: ps[i])
    adj = [0.0] * m
    running = 0.0
    for rank, i in enumerate(idx):
        running = max(running, min(1.0, (m - rank) * ps[i]))
        adj[i] = running
    return adj


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", default="results/runs.csv")
    ap.add_argument("--tag", required=True, help="tag prefix used in io_mpi.sh (rows are TAG_gather, TAG_mpiio)")
    ap.add_argument("--job", help="only rows of this Slurm job id")
    ap.add_argument("--boot", type=int, default=20000, help="bootstrap resamples")
    ap.add_argument("--out", help="also write the table as Markdown to this file")
    a = ap.parse_args()

    random.seed(1)
    data = {}
    for r in csv.DictReader(open(a.csv)):
        if r["tag"] not in (a.tag + "_gather", a.tag + "_mpiio"):
            continue
        if a.job and r["slurm_jobid"] != a.job:
            continue
        key = (int(r["nx"]), int(r["nodes"]), int(r["ranks"]), r["slurm_jobid"])
        data.setdefault(key, {}).setdefault(r["tag"].rsplit("_", 1)[1], []).append(float(r["time_io"]))
    if not data:
        sys.exit(f"no rows with tag {a.tag}_gather/_mpiio" + (f" in job {a.job}" if a.job else ""))

    rows = []
    for key in sorted(data):
        g, m = data[key].get("gather", []), data[key].get("mpiio", [])
        if len(g) != len(m) or not g:
            sys.exit(f"{key}: {len(g)} gather vs {len(m)} mpiio rows; pairs need equal counts")
        ratios = [mi / gi for gi, mi in zip(g, m)]
        boot = sorted(st.median(random.choices(ratios, k=len(ratios))) for _ in range(a.boot))
        lo, hi = boot[int(0.025 * a.boot)], boot[int(0.975 * a.boot) - 1]
        p = wilcoxon_p([math.log(x) for x in ratios])
        rows.append(dict(nx=key[0], nodes=key[1], ranks=key[2], job=key[3], n=len(g),
                         g=st.median(g), m=st.median(m), mb=key[0] ** 2 * 4 / 1e6,
                         ratio=st.median(ratios), lo=lo, hi=hi,
                         faster=sum(x < 1 for x in ratios), p=p))
    for r, q in zip(rows, holm([r["p"] for r in rows])):
        r["p_holm"] = q

    table = ["| Grid | Nodes × ranks | Pairs | gather median (s) | MPI-IO median (s) | "
             "MPI-IO / gather (paired median) | 95% range | MPI-IO faster | Wilcoxon p | Holm-adjusted p |",
             "|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        table.append(f"| {r['nx']}² ({r['mb']:.0f} MB) | {r['nodes']} × {r['ranks'] // r['nodes']} | {r['n']} | "
                     f"{r['g']:.3f} | {r['m']:.3f} | {r['ratio']:.2f} | {r['lo']:.2f}–{r['hi']:.2f} | "
                     f"{r['faster']}/{r['n']} | {r['p']:.3f} | {r['p_holm']:.3f} |")
    jobs = sorted({r["job"] for r in rows})
    table.append("")
    table.append(f"Rows `{a.tag}_gather` / `{a.tag}_mpiio`, Slurm job(s) {', '.join(jobs)}. "
                 "Pairs = alternating repeats; ratio < 1 means MPI-IO faster. "
                 "Holm-adjusted p < 0.05 = significant after correcting for testing several configurations.")
    print("\n".join(table))
    if a.out:
        Path(a.out).parent.mkdir(parents=True, exist_ok=True)
        Path(a.out).write_text("\n".join(table) + "\n")
        print(f"\nwrote {a.out}")


if __name__ == "__main__":
    main()
