#!/usr/bin/env python3
"""Compare mean temperature around Puerta del Sol with El Retiro.
Usage: python3 compare_sol_retiro.py CITY_FILE FIELD_FILE
Positions are fractions of the district box (west -3.710, east -3.672, south 40.406, north 40.426),
so they are approximate. Window = 41 x 41 cells (about 200 m at 5 m cells)."""
import sys
import numpy as np

def read_header(f):
    nx, ny = np.fromfile(f, dtype="<i4", count=2)
    return int(nx), int(ny)

with open(sys.argv[1], "rb") as f:
    nx, ny = read_header(f)
    city = np.fromfile(f, dtype=np.int8, count=nx * ny).reshape(ny, nx)
with open(sys.argv[2], "rb") as f:
    fx, fy = read_header(f)
    T = np.fromfile(f, dtype="<f4", count=fx * fy).reshape(fy, fx)
assert (fx, fy) == (nx, ny), "city and field sizes differ"

W, S, E, N = -3.710, 40.406, -3.672, 40.426
places = {"Puerta del Sol": (-3.7038, 40.4169), "El Retiro (centre)": (-3.6830, 40.4153),
          "Retiro, near the lake": (-3.6818, 40.4152), "Plaza Mayor": (-3.7074, 40.4155)}
names = ["road", "building", "grass", "tree", "water", "green_roof"]
h = 20
print(f"field after the run: min {T.min():.2f}  mean {T.mean():.2f}  max {T.max():.2f} C   (grid {nx} x {ny})")
for name, (lon, lat) in places.items():
    i = int(round((lon - W) / (E - W) * nx)); j = int(round((N - lat) / (N - S) * ny))
    i0, i1, j0, j1 = max(0, i - h), min(nx, i + h + 1), max(0, j - h), min(ny, j + h + 1)
    if i1 <= i0 or j1 <= j0:
        print(f"{name:24s} outside the district box"); continue
    c, t = city[j0:j1, i0:i1], T[j0:j1, i0:i1]
    mix = ", ".join(f"{n} {100*(c==k).mean():.0f}%" for k, n in enumerate(names) if (c == k).any())
    print(f"{name:24s} mean {t.mean():.2f} C  max {t.max():.2f} C   [{mix}]")
