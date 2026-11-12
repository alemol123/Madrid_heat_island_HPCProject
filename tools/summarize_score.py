#!/usr/bin/env python3
"""Summarise the OpenMP scaling runs of score_candidates (results/score_scaling.csv).

The 'schedule' column can contain a comma (dynamic,4), so each line is read from the ends:
tag, threads, then the schedule, then 10 more numbers (nx ny candidates radius selected
t_read t_score t_select t_write t_total).

Output: results/score_scaling_summary.csv and results/score_scaling.png, plus a text summary.
Usage: python3 tools/summarize_score.py [--csv results/score_scaling.csv] [--outdir results]
"""
import argparse
import csv
import os
import statistics as st
import sys

KEYS = ["nx", "ny", "candidates", "radius", "selected", "t_read", "t_score", "t_select", "t_write", "t_total"]


def parse(path):
    rows = []
    with open(path) as f:
        for line in f:
            p = line.strip().split(",")
            if len(p) < 13 or p[0] == "tag":
                continue
            r = {"tag": p[0], "threads": int(p[1]), "schedule": ",".join(p[2:len(p) - 10]).strip('"')}
            r.update({k: float(v) for k, v in zip(KEYS, p[len(p) - 10:])})
            rows.append(r)
    return rows


def summarize(rows):
    """Median over repetitions per (schedule, threads), plus speedups against 1 thread (dynamic,4)."""
    g = {}
    for r in rows:
        g.setdefault((r["schedule"], r["threads"]), []).append(r)
    out = []
    if not any(k == ("dynamic,4", 1) for k in g):
        sys.exit("error: need the 'dynamic,4' run with 1 thread as the baseline")
    for (sch, t), rs in sorted(g.items()):
        out.append({"schedule": sch, "threads": t, "reps": len(rs),
                    **{k: st.median(x[k] for x in rs) for k in ("t_score", "t_select", "t_total")},
                    "t_total_min": min(x["t_total"] for x in rs), "t_total_max": max(x["t_total"] for x in rs)})
    base = {k: next((o[k] for o in out if o["schedule"] == "dynamic,4" and o["threads"] == 1), None)
            for k in ("t_score", "t_total")}
    for o in out:
        for k in ("t_score", "t_total"):
            o["speedup_" + k[2:]] = base[k] / o[k] if base[k] else float("nan")
        o["efficiency_score"] = o["speedup_score"] / o["threads"]
    return out


def amdahl(out):
    """Serial fraction of the 1-thread run (everything except the parallel scoring loop) and the speed-up limit."""
    one = next((o for o in out if o["schedule"] == "dynamic,4" and o["threads"] == 1), None)
    if not one:
        return None
    serial = one["t_total"] - one["t_score"]
    f = serial / one["t_total"]
    return {"serial_s": serial, "serial_fraction": f, "max_speedup": 1.0 / f, "select_share": one["t_select"] / one["t_total"]}


def figure(out, path):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    sc = [o for o in out if o["schedule"] == "dynamic,4"]
    th = [o["threads"] for o in sc]
    fig, ax = plt.subplots(1, 3, figsize=(14, 4.2))
    ax[0].plot(th, th, "--", color="#999", label="ideal")
    ax[0].plot(th, [o["speedup_score"] for o in sc], "o-", color="#2a9d8f", label="scoring loop (OpenMP)")
    ax[0].plot(th, [o["speedup_total"] for o in sc], "s-", color="#e76f51", label="whole program")
    ax[0].set_xscale("log", base=2); ax[0].set_yscale("log", base=2)
    ax[0].set_xlabel("threads"); ax[0].set_ylabel("speed-up vs 1 thread"); ax[0].legend(frameon=False, fontsize=8)
    ax[0].set_title("Speed-up (4096 x 4096, radius 40)", fontsize=10)
    ax[1].bar(range(len(sc)), [o["t_score"] for o in sc], color="#2a9d8f", label="scoring (parallel)")
    ax[1].bar(range(len(sc)), [o["t_total"] - o["t_score"] for o in sc], bottom=[o["t_score"] for o in sc],
              color="#e76f51", label="read + select + write (serial)")
    ax[1].set_xticks(range(len(sc))); ax[1].set_xticklabels(th)
    ax[1].set_xlabel("threads"); ax[1].set_ylabel("seconds"); ax[1].legend(frameon=False, fontsize=8)
    ax[1].set_title("Where the time goes", fontsize=10)
    big = max(o["threads"] for o in out)
    sch = [o for o in out if o["threads"] == big]
    ax[2].bar(range(len(sch)), [o["t_score"] for o in sch], color="#6a8caf")
    ax[2].set_xticks(range(len(sch))); ax[2].set_xticklabels([o["schedule"] for o in sch], fontsize=8)
    ax[2].set_ylabel(f"scoring time, {big} threads (s)"); ax[2].set_title("Loop schedule", fontsize=10)
    for a in ax:
        a.grid(alpha=0.25)
    fig.tight_layout(); fig.savefig(path, dpi=160)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--csv", default="results/score_scaling.csv")
    ap.add_argument("--outdir", default="results")
    ap.add_argument("--no-figure", action="store_true")
    a = ap.parse_args()
    rows = parse(a.csv)
    if not rows:
        sys.exit(f"error: no rows in {a.csv}")
    out = summarize(rows)
    os.makedirs(a.outdir, exist_ok=True)
    with open(os.path.join(a.outdir, "score_scaling_summary.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(out[0])); w.writeheader(); w.writerows(out)
    print(f"{'schedule':11s} {'thr':>3s} {'reps':>4s} {'t_score':>8s} {'t_select':>8s} {'t_total':>8s} {'S_score':>8s} {'S_total':>8s} {'eff':>6s}")
    for o in out:
        print(f"{o['schedule']:11s} {o['threads']:3d} {o['reps']:4d} {o['t_score']:8.3f} {o['t_select']:8.3f} {o['t_total']:8.3f} "
              f"{o['speedup_score']:8.2f} {o['speedup_total']:8.2f} {o['efficiency_score']:6.2f}")
    am = amdahl(out)
    if am:
        print(f"serial part of the 1-thread run: {am['serial_s']:.2f} s = {100 * am['serial_fraction']:.1f}% "
              f"(selection alone {100 * am['select_share']:.1f}%) -> Amdahl limit {am['max_speedup']:.1f}x")
    if not a.no_figure:
        p = os.path.join(a.outdir, "score_scaling.png"); figure(out, p); print("figure", p)


if __name__ == "__main__":
    main()