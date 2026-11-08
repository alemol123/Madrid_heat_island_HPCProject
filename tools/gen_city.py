
"""
This file generates synthetic city data. We will first the program with fake data, then replace witb Madrid data. 

Output format (docs/contract.md):
    nx, ny as int32 little-endian, then nx*ny int8 cell types, row-major
    (cell (i, j) = column i, row j is stored at j*nx + i).

Cell types: 0 road, 1 building, 2 grass/park, 3 tree, 4 water, 5 green roof.
The same (nx, ny, seed) always gives the same file.

"""
import argparse
import os
import sys

import numpy as np

ROAD, BUILDING, GRASS, TREE, WATER, GREEN_ROOF = 0, 1, 2, 3, 4, 5


def _axis_layout(n, rng, block_min=10, block_max=18, road_min=2, road_max=3):
    block_id = np.full(n, -1, dtype=np.int32)
    dist = np.full(n, -1, dtype=np.int32)
    pos = int(rng.integers(road_min, road_max + 1))  # start with a road strip
    b = 0
    while pos < n:
        size = int(rng.integers(block_min, block_max + 1))
        end = min(pos + size, n)
        length = end - pos
        idx = np.arange(length)
        block_id[pos:end] = b
        dist[pos:end] = np.minimum(idx, length - 1 - idx)
        b += 1
        pos = end + int(rng.integers(road_min, road_max + 1))
    return block_id, dist, b


def generate(nx, ny, seed, river=None):
    rng = np.random.default_rng(seed)
    col_id, col_dist, ncb = _axis_layout(nx, rng)
    row_id, row_dist, nrb = _axis_layout(ny, rng)

    # One land use per block: 72% buildings, 16% park, 12% mixed (courtyards).
    r = rng.random((nrb, ncb))
    block_kind = np.where(r < 0.72, 0, np.where(r < 0.88, 1, 2)).astype(np.int8)

    in_block = (row_id[:, None] >= 0) & (col_id[None, :] >= 0)
    kind = block_kind[np.maximum(row_id, 0)[:, None], np.maximum(col_id, 0)[None, :]]
    edge = np.minimum(row_dist[:, None], col_dist[None, :])  # distance to block edge

    city = np.full((ny, nx), ROAD, dtype=np.int8)
    # Buildings: block interior (one-cell margin stays paved).
    bld = in_block & (kind == 0) & (edge >= 1)
    city[bld] = BUILDING
    # Parks: grass inside the block (margin paved).
    park = in_block & (kind == 1) & (edge >= 1)
    city[park] = GRASS
    # Mixed blocks: buildings with a grass courtyard in the middle.
    mixed = in_block & (kind == 2) & (edge >= 1)
    city[mixed] = BUILDING
    city[mixed & (edge >= 4)] = GRASS

    # Trees: dense in parks (~22%), sparse on roads next to blocks (~3%).
    u = rng.integers(0, 256, size=(ny, nx), dtype=np.uint8)
    city[park & (u < 56)] = TREE
    city[(city == ROAD) & (u < 8)] = TREE

    # A winding river across the city (skipped on very small grids).
    if river is None:
        river = min(nx, ny) >= 128
    if river:
        x = np.arange(nx)
        centre = ny * float(rng.uniform(0.35, 0.65)) \
            + 0.08 * ny * np.sin(2 * np.pi * x / (0.5 * nx) + float(rng.uniform(0, 6.28)))
        half = max(1.0, 0.012 * ny)
        yy = np.arange(ny)[:, None]
        city[np.abs(yy - centre[None, :]) <= half] = WATER
    return city


def write_city(path, city):
    ny, nx = city.shape
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        np.array([nx, ny], dtype="<i4").tofile(f)
        np.ascontiguousarray(city, dtype=np.int8).tofile(f)


def read_city(path):
    with open(path, "rb") as f:
        nx, ny = np.fromfile(f, dtype="<i4", count=2)
        city = np.fromfile(f, dtype=np.int8, count=int(nx) * int(ny))
    return city.reshape(int(ny), int(nx))


def save_preview(path, city):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import ListedColormap
    cmap = ListedColormap(["#9ca3af", "#b45309", "#86efac", "#15803d", "#3b82f6", "#a3e635"])
    fig, ax = plt.subplots(figsize=(6, 6))
    ax.imshow(city, cmap=cmap, vmin=0, vmax=5, interpolation="nearest")
    ax.set_title("Synthetic city: road, building, grass, tree, water, green roof")
    ax.axis("off")
    fig.savefig(path, dpi=120, bbox_inches="tight")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--nx", type=int, required=True)
    ap.add_argument("--ny", type=int, required=True)
    ap.add_argument("--seed", type=int, default=42)
    ap.add_argument("--out", required=True)
    ap.add_argument("--preview", help="optional PNG preview path")
    ap.add_argument("--no-river", action="store_true")
    a = ap.parse_args()
    if a.nx < 8 or a.ny < 8:
        sys.exit("nx and ny must be at least 8")
    city = generate(a.nx, a.ny, a.seed, river=False if a.no_river else None)
    write_city(a.out, city)
    counts = np.bincount(city.ravel(), minlength=6)
    names = ["road", "building", "grass", "tree", "water", "green_roof"]
    print(f"wrote {a.out}: {a.nx}x{a.ny}, seed {a.seed}, "
          + ", ".join(f"{n} {100*c/city.size:.1f}%" for n, c in zip(names, counts)))
    if a.preview:
        save_preview(a.preview, city)
        print("preview", a.preview)


if __name__ == "__main__":
    main()
