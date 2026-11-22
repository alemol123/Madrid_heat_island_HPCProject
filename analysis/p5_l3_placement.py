#!/usr/bin/env python3
"""Which L3 caches did the MPI ranks use? (P5)

slurm/scaling_mpi.sh runs srun with --cpu-bind=verbose,cores, so the log contains one
line per rank with the CPU mask it was bound to, e.g.
    cpu-bind=MASK - fortran, task  3  3 [12345]: mask 0x10 set
This script maps every mask to CPU ids, looks up each CPU's L3 cache id in the node's
topology (saved with `lscpu -e=CPU,CORE,SOCKET,NODE,CACHE`), and reports for every run:
ranks, number of distinct L3 caches (chiplets) used, total L3 they offer, the grid's
working set per L3, and the measured compute time from results/runs.csv.

Usage (repo root):
  python3 analysis/p5_l3_placement.py --log results/logs/scaling_mpi_31428.out \
          --topology results/fortran_topology.txt --tag cpu32_bind --job 31428 [--l3-mb 32]

Standard library only.
"""
import argparse
import csv
import re
import statistics as st
import sys


def l3_of_cpu(path):
    """CPU id -> L3 id from `lscpu -e=CPU,CORE,SOCKET,NODE,CACHE` output."""
    m = {}
    for line in open(path):
        p = line.split()
        if len(p) < 5 or not p[0].isdigit():
            continue
        m[int(p[0])] = int(p[4].split(":")[-1])
    if not m:
        sys.exit(f"no CPU lines in {path}")
    return m


def mask_cpus(mask):
    v = int(mask, 16)
    return [i for i in range(v.bit_length()) if v >> i & 1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", required=True)
    ap.add_argument("--topology", required=True)
    ap.add_argument("--csv", default="results/runs.csv")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--job", required=True)
    ap.add_argument("--l3-mb", type=float, default=32.0, help="size of one L3 cache (MB)")
    ap.add_argument("--out", help="also write the table as Markdown")
    a = ap.parse_args()

    l3 = l3_of_cpu(a.topology)

    # one placement per "=== ranks=R repeat K" block in the log
    runs, cur = [], None
    for line in open(a.log):
        h = re.match(r"=== ranks=(\d+) repeat (\d+)", line)
        if h:
            cur = {"ranks": int(h.group(1)), "masks": []}
            runs.append(cur)
            continue
        b = re.search(r"cpu-bind=MASK.*task\s+(\d+)\s+\d+\s+\[\d+\]: mask (0x[0-9a-fA-F]+)", line)
        if b and cur is not None:
            cur["masks"].append(b.group(2))
    if not runs:
        sys.exit(f"no '=== ranks=R repeat K' blocks in {a.log}")

    times, nx = {}, None
    for r in csv.DictReader(open(a.csv)):
        if r["tag"] == a.tag and r["slurm_jobid"] == a.job:
            times.setdefault(int(r["ranks"]), []).append(float(r["time_compute"]))
            nx, ny = int(r["nx"]), int(r["ny"])
    if not times:
        sys.exit(f"no rows with tag {a.tag} and job {a.job} in {a.csv}")
    ws = 2 * (nx + 2) * (ny + 2) * 4 / 1e6 + nx * ny / 1e6   # two float grids + int8 city (MB)

    summary = {}
    for run in runs:
        cpus = [c for m in run["masks"] for c in mask_cpus(m)]
        if len(run["masks"]) != run["ranks"]:
            sys.exit(f"ranks={run['ranks']}: found {len(run['masks'])} cpu-bind lines, expected {run['ranks']}")
        caches = {l3[c] for c in cpus}
        summary.setdefault(run["ranks"], set()).add(len(caches))

    t1 = st.median(times[min(times)])
    table = [f"| Ranks | L3 caches used | Total L3 (MB) | Working set per L3 (MB) | Fits? | Compute (s, median) | Speed-up vs {min(times)} rank |",
             "|---|---|---|---|---|---|---|"]
    for r in sorted(times):
        ncache = sorted(summary.get(r, set()))
        n = max(ncache) if ncache else 0
        per = ws / n if n else float("nan")
        t = st.median(times[r])
        table.append(f"| {r} | {'/'.join(map(str, ncache))} | {n * a.l3_mb:.0f} | {per:.1f} | "
                     f"{'yes' if per <= a.l3_mb else 'no'} | {t:.3f} | {t1 / t:.1f}× |")
    table.append("")
    table.append(f"{nx}×{ny}: working set {ws:.0f} MB (two float grids + city). L3 = {a.l3_mb:.0f} MB per cache. "
                 f"'L3 caches used' lists every count seen over the repeats (placement can differ between repeats). "
                 f"Log {a.log}, job {a.job}.")
    print("\n".join(table))
    if a.out:
        open(a.out, "w").write("\n".join(table) + "\n")
        print(f"\nwrote {a.out}")


if __name__ == "__main__":
    main()
