#!/usr/bin/env python3
"""Make the whole scenario sweep and the job lists for the ensemble runner.

Creates scenario city files (via make_scenarios.py) for every strategy x budget x seed, then
writes job lists. A job list has one job per line:   name  city_file  steps  [extra solver args]

Lists written to --outdir (default data/sweep):
    scenarios/*.city        the scenario cities (git-ignored, regenerate any time)
    jobs_science.txt        the real study: baseline + every scenario at --steps (default 1420)
    jobs_uniform.txt        benchmark: --bench-jobs identical-cost jobs (for scaling tests)
    jobs_skewed.txt         benchmark: same jobs, costs vary a lot, random order
    jobs_skewed_sorted.txt  benchmark: same costs, heaviest first (worst case for static scheduling)
    sweep.json              what was made (names, strategy, budget, seed)

Benchmark jobs reuse the scenario cities in turn; only the number of steps changes (the cost),
so the benchmarks measure scheduling, not science. Cost weights for the skewed lists follow a
heavy-tailed pattern (a few jobs cost ~20x the median), fixed by --seed.

Usage:
    python3 tools/make_sweep.py --city data/district_sol_retiro.city --field build/28548/field.bin
"""
import argparse
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_city  # noqa: E402
import make_scenarios as ms  # noqa: E402
import scenario_metrics as sm  # noqa: E402

SHORT = {"random": "rand", "hottest": "hot", "adjacent": "adj", "greenroof": "roof", "scored": "scored"}
RANDOMISED = ("random", "adjacent")          # these use the seed; hottest and greenroof do not


def scenario_list(budgets, seeds):
    """[(strategy, n, seed or None)] for the sweep, in a fixed order."""
    out = []
    for n in budgets:
        for strat in ("random", "hottest", "adjacent", "greenroof"):
            if strat in RANDOMISED:
                out += [(strat, n, s) for s in seeds]
            else:
                out.append((strat, n, None))
    return out


def scenario_name(strategy, n, seed):
    return f"{SHORT[strategy]}_n{n}" + (f"_s{seed}" if seed is not None else "")


def skew_weights(count, rng, max_weight=24):
    """Heavy-tailed integer cost weights in 1..max_weight (median ~2, a few very heavy)."""
    w = np.ceil(rng.pareto(1.2, size=count) + 1.0).astype(int)
    return np.clip(w, 1, max_weight)


def write_jobs(path, jobs):
    with open(path, "w") as f:
        f.write("# name city steps [extra solver args]\n")
        for name, city, steps in jobs:
            f.write(f"{name} {city} {steps}\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--city", required=True, help="baseline city file")
    ap.add_argument("--field", required=True, help="baseline field at --steps (needed for 'hottest')")
    ap.add_argument("--outdir", default="data/sweep")
    ap.add_argument("--budgets", type=int, nargs="+", default=[500, 1000, 2000, 5000, 10000])
    ap.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3, 4, 5])
    ap.add_argument("--steps", type=int, default=1420, help="steps of the science runs (1420 = day-2 peak)")
    ap.add_argument("--bench-jobs", type=int, default=256)
    ap.add_argument("--bench-steps", type=int, default=1420, help="steps of a weight-1 benchmark job")
    ap.add_argument("--bench-uniform-weight", type=int, default=4)
    ap.add_argument("--seed", type=int, default=7, help="seed of the cost weights")
    ap.add_argument("--scored-exe", help="path of the built score_candidates program: adds the heat-relief 'scored' strategy")
    ap.add_argument("--scored-radius", type=int, default=10)
    ap.add_argument("--scored-spacing", type=int, default=2)
    a = ap.parse_args()

    city = sm.read_city(a.city)
    field = sm.read_field(a.field)
    if field.shape != city.shape:
        sys.exit(f"error: field {field.shape} and city {city.shape} differ in size")
    scen_dir = os.path.join(a.outdir, "scenarios")
    os.makedirs(scen_dir, exist_ok=True)

    made = []
    for strat, n, seed in scenario_list(a.budgets, a.seeds):
        new, info = ms.make_scenario(city, strat, n, seed if seed is not None else 1, field)
        name = scenario_name(strat, n, seed)
        path = os.path.join(scen_dir, name + ".city")
        gen_city.write_city(path, new)
        made.append({"name": name, "city": path, "strategy": strat, "n": n, "seed": seed,
                     "cells_changed": info["cells_changed"]})
    if a.scored_exe:                                   # heat-relief scoring (OpenMP program), one city per budget
        for n in a.budgets:
            name = scenario_name("scored", n, None)
            path = os.path.join(scen_dir, name + ".city")
            r = subprocess.run([a.scored_exe, "--city", a.city, "--field", a.field, "--radius", str(a.scored_radius),
                                "--spacing", str(a.scored_spacing), "--top", str(n), "--out", path],
                               capture_output=True, text=True)
            if r.returncode != 0:
                sys.exit(f"error: score_candidates failed: {r.stderr.strip()}")
            changed = int((gen_city.read_city(path) != city).sum())
            made.append({"name": name, "city": path, "strategy": "scored", "n": n, "seed": None,
                         "cells_changed": changed})
    print(f"wrote {len(made)} scenario cities in {scen_dir}")

    science = [("baseline", a.city, a.steps)] + [(m["name"], m["city"], a.steps) for m in made]
    write_jobs(os.path.join(a.outdir, "jobs_science.txt"), science)

    rng = np.random.default_rng(a.seed)
    cities = [m["city"] for m in made]
    count = a.bench_jobs
    weights = skew_weights(count, rng)
    order = rng.permutation(count)
    names = [f"b{k:04d}" for k in range(count)]
    cyc = [cities[k % len(cities)] for k in range(count)]

    uni = [(names[k], cyc[k], a.bench_steps * a.bench_uniform_weight) for k in range(count)]
    skew = [(names[k], cyc[k], a.bench_steps * int(weights[k])) for k in order]
    srt = sorted(skew, key=lambda j: -j[2])
    write_jobs(os.path.join(a.outdir, "jobs_uniform.txt"), uni)
    write_jobs(os.path.join(a.outdir, "jobs_skewed.txt"), skew)
    write_jobs(os.path.join(a.outdir, "jobs_skewed_sorted.txt"), srt)

    with open(os.path.join(a.outdir, "sweep.json"), "w") as f:
        json.dump({"baseline_city": a.city, "science_steps": a.steps, "scenarios": made,
                   "bench_jobs": count, "bench_steps": a.bench_steps,
                   "skew_weights_sum": int(weights.sum()), "uniform_weight": a.bench_uniform_weight}, f, indent=2)
    print(f"science list: {len(science)} jobs; benchmark lists: {count} jobs each")
    print(f"total cost (in weight-1 jobs): uniform {a.bench_uniform_weight * count}, skewed {int(weights.sum())}, "
          f"heaviest job {int(weights.max())}")


if __name__ == "__main__":
    main()