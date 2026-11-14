#!/usr/bin/env python3
"""Tests for tools/score_candidates.c. Run: python3 tests/test_score.py
Compiles the program (needs gcc with OpenMP) and checks it against a plain numpy/python calculation."""
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "tools")
sys.path.insert(0, TOOLS)
import gen_city  # noqa: E402
import scenario_metrics as sm  # noqa: E402

TMP = tempfile.mkdtemp()
EXE = os.path.join(TMP, "score_candidates")


def build():
    """Compile the program. Returns False (skip) if this machine's compiler has no OpenMP (e.g. Apple clang)."""
    r = subprocess.run(["gcc", "-O2", "-fopenmp", "-std=c11", "-Wall", "-Wextra", "-o", EXE,
                        os.path.join(TOOLS, "score_candidates.c"), "-lm"], capture_output=True, text=True)
    if r.returncode != 0 and ("openmp" in r.stderr.lower() or "omp.h" in r.stderr):
        return False
    assert r.returncode == 0, r.stderr
    assert r.stderr == "", "compiler warnings:\n" + r.stderr
    return True


def reference_scores(city, field, radius, thr):
    """Direct double loops, no tricks: the definition in the header of score_candidates.c."""
    ny, nx = city.shape
    exc = np.maximum(field.astype(np.float64) - thr, 0.0)
    out = np.zeros((ny, nx))
    for j in range(ny):
        for i in range(nx):
            if city[j, i] != 0:
                continue
            s = 0.0
            for q in range(max(0, j - radius), min(ny, j + radius + 1)):
                for p in range(max(0, i - radius), min(nx, i + radius + 1)):
                    d = np.hypot(p - i, q - j)
                    if d <= radius:
                        s += (1.0 - d / (radius + 1.0)) * exc[q, p]
            out[j, i] = s
    return out


def make_case(nx=48, ny=36, seed=4):
    city = gen_city.generate(nx, ny, seed)
    rng = np.random.default_rng(seed)
    field = (27.0 + 3.0 * rng.random(city.shape)).astype(np.float32)   # about 1/3 of the cells above 29
    return city, field


def run(args, env_threads=None):
    env = dict(os.environ)
    if env_threads:
        env["OMP_NUM_THREADS"] = str(env_threads)
    r = subprocess.run([EXE] + args, capture_output=True, text=True, env=env)
    assert r.returncode == 0, r.stderr + r.stdout
    return r.stdout


def files(d, city, field):
    cp, fp = os.path.join(d, "c.city"), os.path.join(d, "f.bin")
    gen_city.write_city(cp, city)
    sm.write_field(fp, field)
    return cp, fp


def test_scores_match_reference():
    city, field = make_case()
    with tempfile.TemporaryDirectory() as d:
        cp, fp = files(d, city, field)
        sp = os.path.join(d, "s.bin")
        run(["--city", cp, "--field", fp, "--radius", "5", "--top", "10", "--out", os.path.join(d, "o.city"),
             "--scores", sp])
        got = sm.read_field(sp).astype(np.float64)
    want = reference_scores(city, field, 5, 29.0)
    assert np.allclose(got, want, rtol=2e-4, atol=1e-4), np.abs(got - want).max()
    assert (got[city != 0] == 0).all()                      # only road cells are scored


def test_selection_rules():
    city, field = make_case(80, 60, 9)
    with tempfile.TemporaryDirectory() as d:
        cp, fp = files(d, city, field)
        sp, op = os.path.join(d, "s.bin"), os.path.join(d, "o.city")
        out = run(["--city", cp, "--field", fp, "--radius", "6", "--top", "40", "--spacing", "3", "--out", op,
                   "--scores", sp])
        new = gen_city.read_city(op)
        score = sm.read_field(sp)
    diff = new != city
    ys, xs = np.nonzero(diff)
    assert diff.sum() == 40 and "selected=40/40" in out
    assert (city[diff] == 0).all() and (new[diff] == 3).all()           # trees on road cells only
    pts = list(zip(xs.tolist(), ys.tolist()))
    for a in range(len(pts)):                                           # spacing: no two within 3 cells (square)
        for b in range(a + 1, len(pts)):
            assert max(abs(pts[a][0] - pts[b][0]), abs(pts[a][1] - pts[b][1])) > 3
    # the best-scoring road cell is always chosen
    best = np.unravel_index(np.argmax(score), score.shape)
    assert diff[best]
    # greedy property: no unblocked road cell with a higher score than the weakest chosen one was skipped
    weakest = min(score[y, x] for x, y in pts)
    for j, i in zip(*np.nonzero((city == 0) & (score > weakest) & ~diff)):
        assert any(max(abs(i - x), abs(j - y)) <= 3 for x, y in pts), (i, j)


def test_same_result_for_any_threads_and_schedule():
    city, field = make_case(120, 90, 2)
    outs = []
    with tempfile.TemporaryDirectory() as d:
        cp, fp = files(d, city, field)
        for k, sched in ((1, "static"), (2, "dynamic,1"), (2, "guided"), (2, "static,3")):
            op, sp = os.path.join(d, f"o{k}{sched}.city"), os.path.join(d, f"s{k}{sched}.bin")
            run(["--city", cp, "--field", fp, "--radius", "7", "--top", "200", "--out", op, "--scores", sp,
                 "--threads", str(k), "--schedule", sched])
            outs.append((open(op, "rb").read(), open(sp, "rb").read()))
    assert all(o == outs[0] for o in outs)                  # identical bytes, whatever the threads


def test_threshold_and_exhausted_candidates():
    city, field = make_case()
    with tempfile.TemporaryDirectory() as d:
        cp, fp = files(d, city, field)
        op = os.path.join(d, "o.city")
        out = run(["--city", cp, "--field", fp, "--radius", "4", "--top", "5", "--threshold", "99", "--out", op])
        assert "selected=0/5" in out and "NOTE" in out       # nothing is hot, nothing is planted
        assert np.array_equal(gen_city.read_city(op), city)
        out = run(["--city", cp, "--field", fp, "--radius", "4", "--top", "100000", "--spacing", "0", "--out", op])
        n_road = int((city == 0).sum())
        assert f"selected={n_road}/100000" in out or "NOTE" in out


def test_timing_csv_and_errors():
    city, field = make_case()
    with tempfile.TemporaryDirectory() as d:
        cp, fp = files(d, city, field)
        csvp = os.path.join(d, "t.csv")
        for tag in ("a", "b"):
            run(["--city", cp, "--field", fp, "--radius", "3", "--top", "5", "--out", os.path.join(d, "o.city"),
                 "--timing-csv", csvp, "--tag", tag, "--threads", "2"])
        lines = open(csvp).read().strip().splitlines()
        assert len(lines) == 3 and lines[0].startswith("tag,threads,") and lines[1].split(",")[1] == "2"
        # size mismatch and missing file are refused with a message
        bad = os.path.join(d, "bad.bin")
        sm.write_field(bad, np.zeros((5, 5), dtype=np.float32))
        r = subprocess.run([EXE, "--city", cp, "--field", bad, "--out", os.path.join(d, "x.city")],
                           capture_output=True, text=True)
        assert r.returncode != 0 and "different sizes" in r.stderr
        r = subprocess.run([EXE, "--city", os.path.join(d, "nope"), "--field", fp, "--out", os.path.join(d, "x.city")],
                           capture_output=True, text=True)
        assert r.returncode != 0 and "cannot open" in r.stderr


if __name__ == "__main__":
    if not build():
        print("SKIPPED: this compiler has no OpenMP (on a Mac, gcc is Apple clang). Run this test on the cluster after "
              "'source env/modules.sh cpu', or install OpenMP with: brew install libomp gcc")
        sys.exit(0)
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)