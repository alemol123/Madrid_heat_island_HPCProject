#!/usr/bin/env python3
"""Make a "what if" city: add trees or green roofs to a baseline city file.

Same file format in and out (docs/contract.md). Cell types: 0 road, 1 building,
2 grass, 3 tree, 4 water, 5 green roof.

--n is the BUDGET in cells, the same for every strategy, so strategies can be compared fairly.

Strategies:
    random     N trees on random candidate cells
    hottest    N trees on the candidate cells that are hottest in a baseline field (needs --field)
    adjacent   N trees on candidate cells that touch existing trees or grass (4-neighbours),
               i.e. growing the green that is already there
    greenroof  whole buildings turned into green roofs (type 5), largest building first,
               while the total stays within N cells (a building that does not fit is skipped)

Candidate cells for trees are road cells (street trees). Add --on-grass to allow grass
cells too. Trees are never placed on buildings, water or existing trees.

Usage:
    python3 tools/make_scenarios.py --city data/district_sol_retiro.city --strategy random \\
        --n 500 --seed 1 --out data/scen/random_500_s1.city
    python3 tools/make_scenarios.py --city data/district_sol_retiro.city --strategy hottest \\
        --n 500 --field build/baseline_field.bin --out data/scen/hottest_500.city

A manifest next to the output (<out>.json) records what was done.
Same inputs and seed always give the same file.
"""
import argparse
import hashlib
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_city  # noqa: E402
import scenario_metrics  # noqa: E402

ROAD, BUILDING, GRASS, TREE, WATER, GREEN_ROOF = (gen_city.ROAD, gen_city.BUILDING, gen_city.GRASS,
                                                   gen_city.TREE, gen_city.WATER, gen_city.GREEN_ROOF)
STRATEGIES = ("random", "hottest", "adjacent", "greenroof")

#Where trees can be placed
def candidate_mask(city, on_grass=False):
    m = city == ROAD
    if on_grass:
        m |= city == GRASS
    return m


def touches_green(city):
    green = (city == TREE) | (city == GRASS)
    t = np.zeros_like(green)
    t[1:, :] |= green[:-1, :]
    t[:-1, :] |= green[1:, :]
    t[:, 1:] |= green[:, :-1]
    t[:, :-1] |= green[:, 1:]
    return t


def pick_trees(city, strategy, n, rng, field=None, on_grass=False):
    cand = candidate_mask(city, on_grass)
    if strategy == "adjacent":
        cand = cand & touches_green(city)
    idx = np.flatnonzero(cand.ravel())
    if idx.size <= n:
        return idx
    if strategy == "hottest":
        temps = field.ravel()[idx]
        order = np.lexsort((idx, -temps))           # hottest first, ties by position
        return idx[order[:n]]
    return rng.choice(idx, size=n, replace=False)   # random and adjacent


def pick_green_roofs(city, n):
    from scipy import ndimage
    labels, count = ndimage.label(city == BUILDING)  # 4-connected
    if count == 0:
        return np.array([], dtype=np.int64), 0
    sizes = np.bincount(labels.ravel(), minlength=count + 1)
    order = np.argsort(-sizes[1:], kind="stable") + 1     # label ids, biggest first
    chosen, used = [], 0
    for lab in order:
        s = int(sizes[lab])
        if used + s <= n:
            chosen.append(lab)
            used += s
        if used == n:
            break
    if not chosen:
        return np.array([], dtype=np.int64), 0
    mask = np.isin(labels, chosen)
    return np.flatnonzero(mask.ravel()), len(chosen)


def make_scenario(city, strategy, n, seed=1, field=None, on_grass=False):
    if strategy not in STRATEGIES:
        raise ValueError(f"unknown strategy {strategy!r}; choose from {STRATEGIES}")
    if n < 0:
        raise ValueError("n must be >= 0")
    new = city.copy()
    rng = np.random.default_rng(seed)
    info = {}
    if strategy == "greenroof":
        idx, nb = pick_green_roofs(city, n)
        new.ravel()[idx] = GREEN_ROOF
        info["buildings_converted"] = nb
    else:
        if strategy == "hottest":
            if field is None:
                raise ValueError("strategy 'hottest' needs a baseline field (--field)")
            if field.shape != city.shape:
                raise ValueError(f"field shape {field.shape} does not match city {city.shape}")
        idx = pick_trees(city, strategy, n, rng, field, on_grass)
        new.ravel()[idx] = TREE
    info["cells_changed"] = int(idx.size)
    return new, info


def _sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()[:16]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--city", required=True, help="baseline city file")
    ap.add_argument("--strategy", required=True, choices=STRATEGIES)
    ap.add_argument("--n", type=int, required=True, help="budget in cells")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--field", help="baseline temperature field (needed for 'hottest')")
    ap.add_argument("--on-grass", action="store_true", help="allow trees on grass cells too")
    ap.add_argument("--out", required=True)
    ap.add_argument("--preview", help="optional PNG preview")
    a = ap.parse_args()

    city = scenario_metrics.read_city(a.city)
    field = scenario_metrics.read_field(a.field) if a.field else None
    try:
        new, info = make_scenario(city, a.strategy, a.n, a.seed, field, a.on_grass)
    except ValueError as exc:
        sys.exit(f"error: {exc}")

    gen_city.write_city(a.out, new)
    ny, nx = city.shape
    manifest = {"baseline_city": a.city, "baseline_sha256_16": _sha(a.city), "output_sha256_16": _sha(a.out),
                "strategy": a.strategy, "n_requested": a.n, "seed": a.seed, "nx": nx, "ny": ny,
                "trees_on_grass_allowed": a.on_grass, "field_used": a.field, **info}
    with open(os.path.splitext(a.out)[0] + ".json", "w") as f:
        json.dump(manifest, f, indent=2)

    msg = f"wrote {a.out}: {a.strategy}, {info['cells_changed']} of {a.n} requested cells changed"
    if "buildings_converted" in info:
        msg += f" ({info['buildings_converted']} buildings)"
    print(msg)
    if info["cells_changed"] < a.n and a.strategy != "greenroof":
        print(f"NOTE: only {info['cells_changed']} candidate cells exist; the budget was not fully used.")
    if a.strategy == "greenroof" and info["cells_changed"] < 0.9 * a.n:
        print(f"NOTE: buildings could only fill {info['cells_changed']} of {a.n} cells (whole buildings only).")
    if a.preview:
        gen_city.save_preview(a.preview, new)
        print("preview", a.preview)


if __name__ == "__main__":
    main()
