#!/usr/bin/env python3
"""Tests for tools/rasterize_district.py. Run: python3 tests/test_rasterize_district.py

Part A (painting, cropping, file output) needs only numpy and always runs.
Part B (shapes -> cells) needs geopandas, rasterio and shapely.
"""
import os
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import gen_city  # noqa: E402
import rasterize_district as rd  # noqa: E402


# ---------------- Part A: pure logic ----------------
def test_grid_shape_rounds_up():
    assert rd.grid_shape(0, 0, 100, 50, 5) == (20, 10)
    assert rd.grid_shape(0, 0, 101, 51, 5) == (21, 11)


def test_paint_priority():
    ny, nx = 4, 6
    z = lambda: np.zeros((ny, nx), dtype=bool)
    grass, wood, water, bld, trees = z(), z(), z(), z(), z()
    grass[:, :3] = True            # grass left half
    wood[0, :2] = True             # wood beats grass
    water[1, 1] = True             # water beats grass
    bld[1, 1] = True               # building beats water
    bld[2, 4] = True
    trees[2, 4] = True             # tree must NOT overwrite a building
    trees[3, 5] = True             # tree on plain road is kept
    water[3, 0] = True
    trees[3, 0] = True             # tree must NOT overwrite water
    city = rd.paint({"grass": grass, "wood": wood, "water": water, "buildings": bld, "trees": trees}, ny, nx)
    assert city[0, 0] == rd.TREE and city[0, 2] == rd.GRASS
    assert city[1, 1] == rd.BUILDING
    assert city[2, 4] == rd.BUILDING
    assert city[3, 5] == rd.TREE
    assert city[3, 0] == rd.WATER
    assert city[3, 3] == rd.ROAD


def test_paint_with_missing_layers_is_all_road():
    city = rd.paint({}, 3, 3)
    assert (city == rd.ROAD).all() and city.dtype == np.int8


def test_center_crop_and_small_city():
    c = np.arange(100, dtype=np.int8).reshape(10, 10)
    crop = rd.center_crop(c, 4)
    assert crop.shape == (4, 4) and crop[0, 0] == c[3, 3]
    assert rd.center_crop(c, 50).shape == (10, 10)


def test_percentages_and_warnings():
    city = np.zeros((10, 10), dtype=np.int8)
    p = rd.percentages(city)
    assert abs(p["road"] - 100.0) < 1e-9
    assert len(rd.sanity_warnings(p)) == 3          # no buildings, all road, no green
    good = rd.paint({"buildings": np.arange(100).reshape(10, 10) % 2 == 0,
                     "grass": np.arange(100).reshape(10, 10) % 5 == 0}, 10, 10)
    assert rd.sanity_warnings(rd.percentages(good)) == []


def test_output_file_matches_contract():
    city = rd.paint({"buildings": np.eye(8, 12, dtype=bool)}, 8, 12)   # 12 columns, 8 rows
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.city")
        gen_city.write_city(p, city)
        raw = open(p, "rb").read()
        nx, ny = np.frombuffer(raw[:8], dtype="<i4")
        assert (nx, ny) == (12, 8) and len(raw) == 8 + 12 * 8
        assert np.array_equal(gen_city.read_city(p), city)


# ---------------- Part B: shapes to cells (needs geospatial packages) ----------------
def _have_geo():
    try:
        import geopandas, rasterio, shapely  # noqa: F401
        return True
    except ImportError:
        return False


def test_shapes_to_cells():
    import geopandas as gpd
    from rasterio.transform import from_origin
    from shapely.geometry import LineString, Point, box
    res, nx, ny = 5.0, 40, 30                         # 200 m x 150 m
    minx, maxy = 440000.0, 4475000.0
    transform = from_origin(minx, maxy, res, res)      # north-west corner, row 0 = north
    mk = lambda geoms: gpd.GeoDataFrame(geometry=geoms, crs=rd.CRS_METRIC)
    layers = {
        # 100 m x 50 m building in the NORTH-west: columns 0-19, rows 0-9
        "buildings": mk([box(minx, maxy - 50, minx + 100, maxy)]),
        # park in the south-east: columns 20-39, rows 15-29
        "grass": mk([box(minx + 100, maxy - 150, minx + 200, maxy - 75)]),
        "water": mk([LineString([(minx, maxy - 100), (minx + 200, maxy - 100)])]),
        "trees": mk([Point(minx + 150, maxy - 40)]),
    }
    masks = rd.rasterize_masks(layers, transform, (ny, nx), tree_radius_m=3.0)
    city = rd.paint(masks, ny, nx)
    assert (city == rd.BUILDING).sum() == 20 * 10                  # exactly 200 cells
    assert (city[:10, :20] == rd.BUILDING).all()                   # north-west block => low rows
    assert (city[15:, 20:] == rd.GRASS).sum() > 250                # park present, south-east
    assert (city == rd.WATER).any()                                # river line got a width
    assert city[8, 30] == rd.TREE                                  # tree on plain ground survives


if __name__ == "__main__":
    tests = [(n, f) for n, f in sorted(globals().items()) if n.startswith("test_")]
    for name, fn in tests:
        if name == "test_shapes_to_cells" and not _have_geo():
            print("skip", name, "(geopandas/rasterio not installed)")
            continue
        fn()
        print("ok", name)
