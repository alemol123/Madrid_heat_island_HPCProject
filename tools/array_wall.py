#!/usr/bin/env python3
"""Turn the timing lines of a job-array run (slurm/ensemble_array.sh) into one row of results/ensemble.csv,
with the same columns as the MPI runner, so both can be compared in one table.

wall = last task end - first task start. It includes the time Slurm needs to start the tasks one
after another (the real cost of a job array) but not the time the job waited in the queue.

Usage:
    python3 tools/array_wall.py results/ens_bench/array_skew_64 --workers 64 --tag array_skew_64 --csv results/ensemble.csv
"""
import argparse
import csv
import os
import sys


def read_times(path):
    rows = []
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) == 4:
                rows.append((p[0], float(p[1]), float(p[2]), int(p[3])))
    return rows


def summarize(rows, workers):
    if not rows:
        raise ValueError("no timing lines found")
    wall = max(r[2] for r in rows) - min(r[1] for r in rows)
    total = sum(r[2] - r[1] for r in rows)
    speedup = total / wall
    return {"jobs": len(rows), "wall_s": wall, "sum_job_s": total, "speedup": speedup,
            "efficiency": speedup / workers, "failed": sum(1 for r in rows if r[3] != 0)}


COLUMNS = ["tag", "mode", "nodes", "ranks", "workers", "jobs", "wall_s", "sum_job_s", "speedup", "efficiency",
           "busy_min_s", "busy_max_s", "imbalance", "failed", "slurm_jobid", "jobs_file"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--workers", type=int, required=True, help="the %K limit used for the array")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--expect", type=int, help="number of jobs the array should have run")
    ap.add_argument("--csv", help="append a row to this CSV")
    a = ap.parse_args()

    try:
        s = summarize(read_times(os.path.join(a.outdir, "array_times.txt")), a.workers)
    except (OSError, ValueError) as exc:
        sys.exit(f"error: {exc}")
    if a.expect and s["jobs"] != a.expect:
        print(f"WARNING: expected {a.expect} jobs but found {s['jobs']}; some tasks may still be running.")
    print(f"array: {s['jobs']} jobs, wall {s['wall_s']:.3f} s, sum of job times {s['sum_job_s']:.3f} s, "
          f"speedup {s['speedup']:.2f}, efficiency {s['efficiency']:.3f}, failed {s['failed']}")
    if a.csv:
        row = {"tag": a.tag, "mode": "array", "nodes": "", "ranks": "", "workers": a.workers, "jobs": s["jobs"],
               "wall_s": f"{s['wall_s']:.4f}", "sum_job_s": f"{s['sum_job_s']:.4f}", "speedup": f"{s['speedup']:.3f}",
               "efficiency": f"{s['efficiency']:.4f}", "busy_min_s": "", "busy_max_s": "", "imbalance": "",
               "failed": s["failed"], "slurm_jobid": "", "jobs_file": ""}
        fresh = not os.path.exists(a.csv)
        os.makedirs(os.path.dirname(os.path.abspath(a.csv)), exist_ok=True)
        with open(a.csv, "a", newline="") as f:
            w = csv.DictWriter(f, fieldnames=COLUMNS)
            if fresh:
                w.writeheader()
            w.writerow(row)
        print("appended to", a.csv)


if __name__ == "__main__":
    main()
