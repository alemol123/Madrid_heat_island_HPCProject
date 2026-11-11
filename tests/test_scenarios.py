#!/usr/bin/env python3
"""Tests for tools/make_scenarios.py and tools/scenario_metrics.py.
Run: python3 tests/test_scenarios.py  (needs numpy and scipy)."""
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, "..", "tools")
sys.path.insert(0, TOOLS)
import gen_city  # noqa: E402
import make_scenarios as ms  # noqa: E402
import scenario_metrics as sm  # noqa: E402


def city():
    return gen_city.generate(160, 120, 5)


def test_all_tree_strategies_only_use_road():
    c = city()
    field = np.random.default_rng(0).random(c.shape).astype(np.float32)
    for s in ("random", "hottest", "adjacent"):
        new, info = ms.make_scenario(c, s, 300, seed=2, field=field)
        diff = new != c
        assert info["cells_changed"] == 300 == int(diff.sum()), s
        assert (c[diff] == ms.ROAD).all(), s           # only road cells changed
        assert (new[diff] == ms.TREE).all(), s


def test_on_grass_allows_grass_cells():
    c = city()
    new, _ = ms.make_scenario(c, "random", (c == ms.ROAD).sum() + 50, seed=1, on_grass=True)
    changed_from = set(np.unique(c[new != c]).tolist())
    assert changed_from == {ms.ROAD, ms.GRASS}


def test_deterministic_and_seed_dependent():
    c = city()
    a, _ = ms.make_scenario(c, "random", 200, seed=3)
    b, _ = ms.make_scenario(c, "random", 200, seed=3)
    d, _ = ms.make_scenario(c, "random", 200, seed=4)
    assert np.array_equal(a, b) and not np.array_equal(a, d)


def test_input_city_not_modified():
    c = city()
    keep = c.copy()
    ms.make_scenario(c, "random", 100, seed=1)
    ms.make_scenario(c, "greenroof", 500, seed=1)
    assert np.array_equal(c, keep)


def test_hottest_picks_the_hottest_candidates():
    c = city()
    field = np.zeros(c.shape, dtype=np.float32)
    road = np.flatnonzero((c == ms.ROAD).ravel())
    hot = road[::7][:40]                                  # 40 chosen road cells get the high values
    field.ravel()[hot] = 30.0 + np.arange(40)
    new, _ = ms.make_scenario(c, "hottest", 40, field=field)
    assert set(np.flatnonzero((new != c).ravel()).tolist()) == set(hot.tolist())


def test_hottest_needs_field_and_matching_shape():
    c = city()
    for bad in (None, np.zeros((3, 3), dtype=np.float32)):
        try:
            ms.make_scenario(c, "hottest", 10, field=bad)
        except ValueError:
            continue
        raise AssertionError("expected ValueError")


def test_adjacent_touches_green():
    c = city()
    new, _ = ms.make_scenario(c, "adjacent", 200, seed=1)
    diff = new != c
    assert diff.sum() == 200
    assert ms.touches_green(c)[diff].all()


def test_budget_larger_than_candidates_uses_all():
    c = np.full((10, 10), ms.BUILDING, dtype=np.int8)
    c[0, :5] = ms.ROAD
    new, info = ms.make_scenario(c, "random", 100, seed=1)
    assert info["cells_changed"] == 5 and (new[0, :5] == ms.TREE).all()


def test_greenroof_whole_buildings_within_budget():
    from scipy import ndimage
    c = city()
    n = 1500
    new, info = ms.make_scenario(c, "greenroof", n)
    diff = new != c
    assert 0 < info["cells_changed"] <= n and int(diff.sum()) == info["cells_changed"]
    assert (c[diff] == ms.BUILDING).all() and (new[diff] == ms.GREEN_ROOF).all()
    # each converted building is complete: no building cell left touching a green roof
    labels, _ = ndimage.label(c == ms.BUILDING)
    for lab in np.unique(labels[diff]):
        assert diff[labels == lab].all()
    assert info["buildings_converted"] == len(np.unique(labels[diff]))
    assert info["cells_changed"] >= 0.8 * n               # greedy fill gets close to the budget


def test_greenroof_picks_largest_first():
    c = np.zeros((10, 20), dtype=np.int8)
    c[1:4, 1:4] = ms.BUILDING        # 9 cells
    c[6:8, 1:3] = ms.BUILDING        # 4 cells
    c[1:3, 10:18] = ms.BUILDING      # 16 cells
    new, info = ms.make_scenario(c, "greenroof", 16)
    assert info["buildings_converted"] == 1 and (new == ms.GREEN_ROOF).sum() == 16
    new, info = ms.make_scenario(c, "greenroof", 13)       # 16 does not fit; 9 + 4 do
    assert info["buildings_converted"] == 2 and (new == ms.GREEN_ROOF).sum() == 13


def test_greenroof_with_no_buildings():
    new, info = ms.make_scenario(np.zeros((5, 5), dtype=np.int8), "greenroof", 10)
    assert info["cells_changed"] == 0 and (new == 0).all()


def test_unknown_strategy():
    try:
        ms.make_scenario(city(), "magic", 5)
    except ValueError:
        return
    raise AssertionError("expected ValueError")


def test_field_roundtrip_and_stats():
    with tempfile.TemporaryDirectory() as d:
        f = np.arange(12, dtype=np.float32).reshape(3, 4) + 20      # 20..31
        p = os.path.join(d, "f.bin")
        sm.write_field(p, f)
        assert os.path.getsize(p) == 8 + 12 * 4
        assert np.array_equal(sm.read_field(p), f)
    s = sm.field_stats(f, threshold=29.0)
    assert s["peak"] == 31 and abs(s["mean"] - 25.5) < 1e-9
    assert abs(s["hot_frac"] - 2 / 12) < 1e-9              # 30 and 31 are above 29


def test_compare_changes_and_efficiency():
    base = np.full((10, 10), 28.0, dtype=np.float32)
    base[0, 0] = 29.3
    scen = base - 0.5                                       # everything 0.5 C cooler
    bc = np.zeros((10, 10), dtype=np.int8)
    sc = bc.copy()
    sc[5, 5:9] = 3                                          # 4 changed cells
    r = sm.compare(scen, base, 29.0, sc, bc)
    assert abs(r["d_mean"] + 0.5) < 1e-6 and abs(r["d_peak"] + 0.5) < 1e-6
    assert abs(r["d_hot_frac"] + 0.01) < 1e-9              # the one hot cell drops below 29
    assert r["changed_cells"] == 4
    assert abs(r["cooling_per_cell"] - 0.5 * 100 / 4) < 1e-6


def test_compare_rejects_bad_input():
    a = np.zeros((4, 4), dtype=np.float32)
    for args in ((a, np.zeros((5, 5), dtype=np.float32)),):
        try:
            sm.compare(*args)
        except ValueError:
            pass
        else:
            raise AssertionError("shape mismatch not caught")
    bad = a.copy()
    bad[1, 1] = np.nan
    try:
        sm.compare(bad, a)
    except ValueError:
        return
    raise AssertionError("NaN not caught")


def test_cli_end_to_end():
    with tempfile.TemporaryDirectory() as d:
        base = os.path.join(d, "base.city")
        gen_city.write_city(base, city())
        out = os.path.join(d, "sub", "t.city")
        r = subprocess.run([sys.executable, os.path.join(TOOLS, "make_scenarios.py"), "--city", base,
                            "--strategy", "random", "--n", "100", "--out", out],
                           capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        assert os.path.exists(out) and os.path.exists(os.path.splitext(out)[0] + ".json")
        new = gen_city.read_city(out)
        assert (new != gen_city.read_city(base)).sum() == 100
        fb, fs = os.path.join(d, "b.bin"), os.path.join(d, "s.bin")
        sm.write_field(fb, np.full(new.shape, 28.0, dtype=np.float32))
        sm.write_field(fs, np.full(new.shape, 27.9, dtype=np.float32))
        csvp = os.path.join(d, "r.csv")
        for tag in ("a", "b"):
            r = subprocess.run([sys.executable, os.path.join(TOOLS, "scenario_metrics.py"), "--field", fs,
                                "--baseline", fb, "--city", out, "--baseline-city", base, "--tag", tag,
                                "--csv", csvp], capture_output=True, text=True)
            assert r.returncode == 0, r.stderr
        lines = open(csvp).read().strip().splitlines()
        assert len(lines) == 3 and lines[0].startswith("tag,")


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_"):
            fn()
            print("ok", name)
