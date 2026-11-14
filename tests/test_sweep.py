#!/usr/bin/env python3
"""Tests for tools/make_sweep.py and tools/array_wall.py. Run: python3 tests/test_sweep.py"""
import os
import subprocess
import sys
import tempfile

import numpy as np

TOOLS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools")
sys.path.insert(0, TOOLS)
import array_wall  # noqa: E402
import gen_city  # noqa: E402
import make_sweep as sw  # noqa: E402
import summarize_bench as sb  # noqa: E402
import summarize_sweep as ss  # noqa: E402
import scenario_metrics as sm  # noqa: E402


def read_jobs(path):
    return [l.split() for l in open(path) if l.strip() and not l.startswith("#")]


def test_scenario_list_and_names():
    lst = sw.scenario_list([500, 1000], [1, 2, 3])
    assert len(lst) == 2 * (3 + 1 + 3 + 1)                       # random+adjacent per seed, hottest, greenroof
    names = [sw.scenario_name(*s) for s in lst]
    assert len(set(names)) == len(names)
    assert "hot_n500" in names and "rand_n1000_s3" in names and "roof_n500" in names


def test_skew_weights_are_heavy_tailed_and_bounded():
    w = sw.skew_weights(2000, np.random.default_rng(1))
    assert w.min() >= 1 and w.max() <= 24
    assert np.median(w) <= 3 and w.max() >= 12


def test_cli_makes_consistent_lists():
    with tempfile.TemporaryDirectory() as d:
        city = gen_city.generate(120, 100, 3)
        cpath, fpath = os.path.join(d, "c.city"), os.path.join(d, "f.bin")
        gen_city.write_city(cpath, city)
        sm.write_field(fpath, np.random.default_rng(0).random(city.shape).astype(np.float32) + 25)
        out = os.path.join(d, "sw")
        r = subprocess.run([sys.executable, os.path.join(TOOLS, "make_sweep.py"), "--city", cpath, "--field", fpath,
                            "--outdir", out, "--budgets", "50", "100", "--seeds", "1", "2", "--bench-jobs", "40"],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        sci = read_jobs(os.path.join(out, "jobs_science.txt"))
        assert sci[0][0] == "baseline" and len(sci) == 1 + 2 * (2 + 1 + 2 + 1)
        assert all(os.path.exists(j[1]) and j[2] == "1420" for j in sci)
        uni, skew, srt = (read_jobs(os.path.join(out, f"jobs_{k}.txt")) for k in ("uniform", "skewed", "skewed_sorted"))
        assert len(uni) == len(skew) == len(srt) == 40
        assert sorted(j[0] for j in skew) == sorted(j[0] for j in srt)
        steps = [int(j[2]) for j in srt]
        assert steps == sorted(steps, reverse=True)                # heaviest first
        assert len({j[2] for j in uni}) == 1                       # uniform: one cost
        assert abs(sum(map(int, (j[2] for j in uni))) - sum(steps)) / sum(steps) < 0.35   # comparable total cost
        # the scenario files are real, valid cities
        c = gen_city.read_city(sci[1][1])
        assert c.shape == city.shape


def test_summarize_groups_and_std():
    rows = []
    for seed, v in ((1, -0.10), (2, -0.20)):
        rows.append({"name": f"r{seed}", "strategy": "random", "n": 500, "seed": seed,
                     **{m: v for m in ss.METRICS}})
    rows.append({"name": "h", "strategy": "hottest", "n": 500, "seed": "", **{m: -0.3 for m in ss.METRICS}})
    out = ss.summarize(rows)
    assert [(o["strategy"], o["runs"]) for o in out] == [("hottest", 1), ("random", 2)]
    r = out[1]
    assert abs(r["d_mean_mean"] + 0.15) < 1e-12 and abs(r["d_mean_std"] - 0.0707106781) < 1e-9
    assert out[0]["d_mean_std"] == 0.0                              # single run: no spread


def test_bench_tag_parsing_and_grouping():
    assert sb.parse_tag("bench_skewed_sorted_static_w31_n1_r2") == ("skewed_sorted", "static", 31, 1, 2)
    assert sb.parse_tag("bench_uniform_dynamic_w31_n2_r1") == ("uniform", "dynamic", 31, 2, 1)
    assert sb.parse_tag("ens_dynamic") is None and sb.parse_tag("array_uniform_w32_r1") is None
    runs = [{"list": "uniform", "mode": "dynamic", "w": 4, "nodes": 1, "rep": r, "failed": 0.0, "wall_s": w,
             "sum_job_s": 10.0, "efficiency": 0.9, "imbalance": 1.0} for r, w in ((1, 10.0), (2, 12.0), (3, 30.0))]
    runs.append({**runs[0], "rep": 4, "wall_s": 999.0, "failed": 1.0})        # failed runs are ignored
    g = sb.group(runs)[("uniform", "dynamic", 4, 1)]
    assert g["reps"] == 3 and g["wall"] == 12.0 and g["wall_min"] == 10.0 and g["wall_max"] == 30.0


def test_cli_with_scored_strategy():
    with tempfile.TemporaryDirectory() as d:
        exe = os.path.join(d, "score_candidates")
        r = subprocess.run(["gcc", "-O2", "-fopenmp", "-std=c11", "-o", exe, os.path.join(TOOLS, "score_candidates.c"), "-lm"],
                           capture_output=True, text=True)
        if r.returncode != 0 and ("openmp" in r.stderr.lower() or "omp.h" in r.stderr):
            print("skip test_cli_with_scored_strategy: this compiler has no OpenMP (run it on the cluster)")
            return
        assert r.returncode == 0, r.stderr
        city = gen_city.generate(120, 100, 3)
        cpath, fpath = os.path.join(d, "c.city"), os.path.join(d, "f.bin")
        gen_city.write_city(cpath, city)
        sm.write_field(fpath, (27 + 3 * np.random.default_rng(0).random(city.shape)).astype(np.float32))
        out = os.path.join(d, "sw")
        r = subprocess.run([sys.executable, os.path.join(TOOLS, "make_sweep.py"), "--city", cpath, "--field", fpath,
                            "--outdir", out, "--budgets", "50", "100", "--seeds", "1", "--bench-jobs", "20",
                            "--scored-exe", exe], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        sci = read_jobs(os.path.join(out, "jobs_science.txt"))
        names = [j[0] for j in sci]
        assert "scored_n50" in names and "scored_n100" in names and len(sci) == 1 + 2 * 4 + 2
        new = gen_city.read_city(os.path.join(out, "scenarios", "scored_n100.city"))
        assert int((new != city).sum()) == 100 and (city[new != city] == 0).all()


def test_array_wall():
    rows = [("a", 100.0, 102.0, 0), ("b", 100.5, 101.5, 0), ("c", 102.0, 104.0, 3)]
    s = array_wall.summarize(rows, workers=2)
    assert abs(s["wall_s"] - 4.0) < 1e-9 and abs(s["sum_job_s"] - 5.0) < 1e-9
    assert abs(s["speedup"] - 1.25) < 1e-9 and abs(s["efficiency"] - 0.625) < 1e-9 and s["failed"] == 1
    try:
        array_wall.summarize([], 2)
    except ValueError:
        return
    raise AssertionError("empty input accepted")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)