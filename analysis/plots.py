#!/usr/bin/env python3
"""Shared plot library (P5): plots and tables from results/runs.csv.

Same style as analysis/p1_plots.py, so all report figures match. Every plot uses
the mean +- std of the repeats (contract: >= 3 per configuration) and writes a
Markdown table next to the PNG (same name, .md), ready for the report.

Usage (from the repo root):
  python3 analysis/plots.py speedup     --tags mpi_strong [--x cores|ranks|threads|gpus|nodes]
  python3 analysis/plots.py breakdown   --tags serial mpi openacc ...
  python3 analysis/plots.py compare     --tags flags_O2 flags_O3 ... [--metric time_compute]
  python3 analysis/plots.py sensitivity --tags sizes [--x nx] [--metric ns]

Common options:
  --csv results/runs.csv     input file
  --nx N                     only rows with this grid size
  --out results/plots/X.png  output (default: results/plots/<command>.png)
  --title "..."              plot title
  --labels A B ...           display names for the tags (same order as --tags)

Speed-up and efficiency are relative to the smallest resource count in each
series. Needs pandas and matplotlib (see env/requirements-analysis.txt).
"""
import argparse
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

# ---------------- style (same as analysis/p1_plots.py) ----------------
BLUE, INK, INK2, GRID, SURF = "#2a78d6", "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"
SERIES = [BLUE, "#e07b39", "#3a9a5b", "#7a5cc4", "#8a8984", "#c44e52"]
PARTS = [("time_compute", "compute", BLUE),
         ("time_comm", "communication", "#e07b39"),
         ("time_transfer", "CPU-GPU transfer", "#c44e52"),
         ("time_io", "I/O", "#8a8984")]

plt.rcParams.update({
    "font.size": 10, "axes.edgecolor": INK2, "axes.labelcolor": INK, "text.color": INK,
    "xtick.color": INK2, "ytick.color": INK2, "axes.spines.top": False,
    "axes.spines.right": False, "figure.facecolor": SURF, "axes.facecolor": SURF,
    "savefig.facecolor": SURF,
})

TIMES = ["time_total", "time_compute", "time_comm", "time_io", "time_transfer"]
LABELS = {"ns": "Compute time per cell update (ns)", "time_total": "Total time (s)",
          "time_compute": "Compute time (s)", "time_comm": "Communication time (s)",
          "time_io": "I/O time (s)", "time_transfer": "CPU-GPU transfer time (s)",
          "cores": "CPU cores (ranks × threads)", "ranks": "MPI ranks", "threads": "OpenMP threads",
          "gpus": "GPUs", "nodes": "Nodes", "nx": "Grid size n (n × n cells)", "steps": "Time steps"}


# ---------------- shared loading ----------------
def load(csv, tags, nx=None):
    """Rows for the given tags (and grid size), with derived columns."""
    d = pd.read_csv(csv)
    for col in TIMES:
        if col not in d:
            d[col] = 0.0
        d[col] = d[col].fillna(0.0)
    if "node" not in d:
        d["node"] = "?"
    d = d[d.tag.isin(tags)]
    if nx is not None:
        d = d[d.nx == nx]
    if d.empty:
        sys.exit(f"no rows in {csv} for tags {tags}" + (f" and nx={nx}" if nx else ""))
    d = d.copy()
    d["ns"] = 1e9 * d.time_compute / (d.nx * d.ny * d.steps)
    d["cores"] = d.ranks * d.threads
    d["order"] = d.tag.map({t: i for i, t in enumerate(tags)})
    return d


def summarize(d, keys):
    """Mean +- std of the repeats per configuration. Warns about too few repeats
    or repeats with different checksums (they should be identical)."""
    agg = {"reps": ("time_total", "size"), "checksums": ("checksum", "nunique"),
           "node": ("node", "first"), "order": ("order", "first")}
    for col in TIMES + ["ns"]:
        agg[col] = (col, "mean")
        agg[col + "_std"] = (col, "std")
    s = d.groupby(keys).agg(**agg).reset_index()
    stds = [c for c in s.columns if c.endswith("_std")]
    s[stds] = s[stds].fillna(0.0)
    for r in s[s.checksums > 1].itertuples():
        print(f"WARNING: repeats of {r.tag} gave {r.checksums} different checksums", file=sys.stderr)
    if (s.reps < 3).any():
        print("WARNING: some configurations have fewer than 3 repeats (contract asks for >= 3)",
              file=sys.stderr)
    return s.sort_values(["order"] + [k for k in keys if k != "tag"]).reset_index(drop=True)


def fmt(m, sd, p=3):
    return f"{m:.{p}f} ± {sd:.{p}f}"


def name_of(tag, a):
    """Display name: --labels if given, else the tag."""
    if a.labels and tag in a.tags:
        i = a.tags.index(tag)
        if i < len(a.labels):
            return a.labels[i]
    return tag


def finish(fig, axes, out, title, table):
    for ax in axes:
        ax.grid(color=GRID, lw=0.8)
        ax.set_axisbelow(True)
    if title:
        axes[0].set_title(title, fontsize=10, loc="left")
    fig.tight_layout()
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=200, bbox_inches="tight")
    plt.close(fig)
    md = out.with_suffix(".md")
    md.write_text("\n".join(table) + "\n")
    print("\n".join(table))
    print(f"\nwrote {out} and {md}")


# ---------------- speedup + efficiency ----------------
def cmd_speedup(a):
    d = load(a.csv, a.tags, a.nx)
    s = summarize(d, ["tag", "nx", a.x])
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(9, 3.8))
    table = [f"| Run | Grid | {LABELS[a.x]} | Compute (s) | Speed-up | Efficiency |",
             "|---|---|---|---|---|---|"]
    for color, ((tag, nx), g) in zip(SERIES, s.groupby(["tag", "nx"], sort=False)):
        g = g.sort_values(a.x)
        p0, t0 = g[a.x].iloc[0], g.time_compute.iloc[0]
        sp = t0 / g.time_compute
        eff = sp / (g[a.x] / p0)
        label = f"{name_of(tag, a)} ({nx}²)"
        ax1.plot(g[a.x], sp, marker="o", color=color, lw=2, mec=SURF, mew=1.5, label=label)
        ax2.plot(g[a.x], eff, marker="o", color=color, lw=2, mec=SURF, mew=1.5, label=label)
        for r, spi, ei in zip(g.itertuples(), sp, eff):
            table.append(f"| {name_of(tag, a)} | {nx}×{nx} | {getattr(r, a.x)} | "
                         f"{fmt(r.time_compute, r.time_compute_std)} | {spi:.2f}× | {100 * ei:.0f}% |")
    xs = sorted(s[a.x].unique())
    ax1.plot(xs, [x / xs[0] for x in xs], ls=(0, (3, 3)), color=INK2, lw=1, label="ideal")
    ax2.axhline(1.0, ls=(0, (3, 3)), color=INK2, lw=1)
    for ax in (ax1, ax2):
        ax.set_xscale("log", base=2)
        ax.set_xticks(xs)
        ax.set_xticklabels([str(x) for x in xs])
        ax.set_xlabel(LABELS[a.x])
    ax1.set_ylabel("Speed-up")
    ax2.set_ylabel("Parallel efficiency")
    ax2.set_ylim(0, 1.1)
    ax1.legend(frameon=False, fontsize=8)
    finish(fig, [ax1, ax2], a.out, a.title, table)


# ---------------- time breakdown ----------------
def cmd_breakdown(a):
    d = load(a.csv, a.tags, a.nx)
    s = summarize(d, ["tag", "nx", "nodes", "ranks", "threads", "gpus"])
    s["label"] = [f"{name_of(r.tag, a)}\n{r.nx}², {r.ranks}×{r.threads}" + (f", {r.gpus} GPU" if r.gpus else "")
                  for r in s.itertuples()]
    known = sum(s[col] for col, _, _ in PARTS)
    s["time_other"] = (s.time_total - known).clip(lower=0)
    fig, ax = plt.subplots(figsize=(7, 0.6 * len(s) + 1.6))
    y = list(range(len(s)))[::-1]
    left = [0.0] * len(s)
    for col, name, color in PARTS + [("time_other", "other (setup)", "#b5b2aa")]:
        vals = s[col].tolist()
        if not any(v > 0 for v in vals):
            continue
        ax.barh(y, vals, left=left, color=color, height=0.55, label=name)
        left = [l + v for l, v in zip(left, vals)]
    for yi, tot in zip(y, s.time_total):
        ax.text(tot * 1.01, yi, f"{tot:.3g} s", va="center", fontsize=8, color=INK)
    ax.set_yticks(y)
    ax.set_yticklabels(s.label, fontsize=8)
    ax.set_xlim(0, s.time_total.max() * 1.18)
    ax.set_xlabel("Time (s), mean of repeats")
    ax.legend(frameon=False, fontsize=8, loc="upper center", bbox_to_anchor=(0.5, -0.30), ncol=5)
    table = ["| Run | Compute (s) | Communication (s) | Transfer (s) | I/O (s) | Total (s) |",
             "|---|---|---|---|---|---|"]
    for r in s.itertuples():
        table.append(f"| {r.label.replace(chr(10), ', ')} | {r.time_compute:.3f} | {r.time_comm:.3f} | "
                     f"{r.time_transfer:.3f} | {r.time_io:.3f} | {fmt(r.time_total, r.time_total_std)} |")
    finish(fig, [ax], a.out, a.title, table)


# ---------------- before / after comparison ----------------
def cmd_compare(a):
    d = load(a.csv, a.tags, a.nx)
    if d.nx.nunique() > 1:
        sys.exit(f"several grid sizes {sorted(d.nx.unique())}: choose one with --nx")
    s = summarize(d, ["tag", "nx"])
    base = s[a.metric].iloc[0]
    fig, ax = plt.subplots(figsize=(7, 0.55 * len(s) + 1.6))
    y = list(range(len(s)))[::-1]
    ax.barh(y, s[a.metric], xerr=s[a.metric + "_std"], color=BLUE, height=0.55, capsize=3)
    for yi, v, e in zip(y, s[a.metric], s[a.metric + "_std"]):
        ax.text(v + e + base * 0.03, yi, f"{v:.3g}  ({base / v:.2f}×)", va="center", fontsize=9)
    ax.set_yticks(y)
    ax.set_yticklabels([name_of(t, a) for t in s.tag])
    ax.set_xlim(0, s[a.metric].max() * 1.35)
    ax.set_xlabel(f"{LABELS[a.metric]}, {s.nx.iloc[0]} × {s.nx.iloc[0]}")
    table = [f"| Variant | {LABELS[a.metric]} | Speed-up vs {name_of(s.tag.iloc[0], a)} |", "|---|---|---|"]
    for r in s.itertuples():
        table.append(f"| {name_of(r.tag, a)} | {fmt(getattr(r, a.metric), getattr(r, a.metric + '_std'))} | "
                     f"{base / getattr(r, a.metric):.2f}× |")
    finish(fig, [ax], a.out, a.title, table)


# ---------------- sensitivity ----------------
def cmd_sensitivity(a):
    d = load(a.csv, a.tags, a.nx)
    s = summarize(d, ["tag", a.x])
    fig, ax = plt.subplots(figsize=(7, 4))
    table = [f"| Run | {LABELS.get(a.x, a.x)} | {LABELS[a.metric]} |", "|---|---|---|"]
    for color, (tag, g) in zip(SERIES, s.groupby("tag", sort=False)):
        g = g.sort_values(a.x)
        ax.errorbar(g[a.x], g[a.metric], yerr=g[a.metric + "_std"], color=color, lw=2, marker="o",
                    ms=6, mec=SURF, mew=1.5, capsize=3, label=name_of(tag, a))
        for r in g.itertuples():
            table.append(f"| {name_of(tag, a)} | {getattr(r, a.x)} | "
                         f"{fmt(getattr(r, a.metric), getattr(r, a.metric + '_std'))} |")
    if a.logx:
        xs = sorted(s[a.x].unique())
        ax.set_xscale("log", base=2)
        ax.set_xticks(xs)
        ax.set_xticklabels([str(x) for x in xs], rotation=45)
    ax.set_xlabel(LABELS.get(a.x, a.x))
    ax.set_ylabel(LABELS[a.metric])
    if s.tag.nunique() > 1:
        ax.legend(frameon=False, fontsize=8)
    finish(fig, [ax], a.out, a.title, table)


def main():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--csv", default="results/runs.csv", type=Path)
    common.add_argument("--tags", nargs="+", required=True)
    common.add_argument("--labels", nargs="+")
    common.add_argument("--nx", type=int)
    common.add_argument("--out", type=Path)
    common.add_argument("--title")
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    sp = sub.add_parser("speedup", parents=[common])
    sp.add_argument("--x", default="cores", choices=["cores", "ranks", "threads", "gpus", "nodes"])
    sub.add_parser("breakdown", parents=[common])
    cp = sub.add_parser("compare", parents=[common])
    cp.add_argument("--metric", default="time_compute", choices=TIMES + ["ns"])
    se = sub.add_parser("sensitivity", parents=[common])
    se.add_argument("--x", default="nx")
    se.add_argument("--metric", default="ns", choices=TIMES + ["ns"])
    se.add_argument("--logx", action="store_true")
    a = p.parse_args()
    if a.out is None:
        a.out = Path("results/plots") / f"{a.cmd}.png"
    {"speedup": cmd_speedup, "breakdown": cmd_breakdown,
     "compare": cmd_compare, "sensitivity": cmd_sensitivity}[a.cmd](a)


if __name__ == "__main__":
    main()