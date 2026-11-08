
"""Rasterize a real Madrid district from OpenStreetMap into a city file.

Writes the city-file format in docs/contract.md: nx, ny as int32 little-endian,
then nx*ny int8 cell types, row-major. Row 0 is the NORTH edge, column 0 the WEST edge.
Cell types: 0 road, 1 building, 2 grass/park, 3 tree, 4 water, 5 green roof.

Paint order (later layers win): everything starts as road/paved (0), then
grass and parks (2), woods (3), water (4), buildings (1); finally single trees (3),
which never overwrite buildings or water.

Usage (run on your laptop, not on the cluster; needs internet):
    python3 tools/rasterize_district.py --bbox -3.710 40.406 -3.672 40.426 --res 5 \\
        --out data/district_sol_retiro.city --preview data/district_sol_retiro.png --sample 256

--bbox is west south east north in degrees (lon/lat). The default box runs from
Puerta del Sol to El Retiro (about 3.2 km x 2.2 km, roughly 640 x 445 cells at 5 m).

Needs: numpy, matplotlib, osmnx, geopandas, rasterio, shapely, pyproj.
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_city  # noqa: E402  (write_city, save_preview, cell-type constants)

CRS_METRIC = "EPSG:25830"   # ETRS89 / UTM zone 30N, metres, the Spanish standard
ROAD, BUILDING, GRASS, TREE, WATER = (gen_city.ROAD, gen_city.BUILDING, gen_city.GRASS,
                                      gen_city.TREE, gen_city.WATER)
NAMES = ["road", "building", "grass", "tree", "water", "green_roof"]

# OpenStreetMap tags for each layer.
OSM_QUERIES = {
    "buildings": {"building": True},
    "grass": {"leisure": ["park", "garden", "pitch", "playground", "recreation_ground", "common"],
              "landuse": ["grass", "meadow", "recreation_ground", "village_green", "cemetery", "orchard"],
              "natural": ["grassland", "scrub", "heath"]},
    "wood": {"natural": ["wood"], "landuse": ["forest"]},
    "water": {"natural": ["water"], "landuse": ["reservoir", "basin"],
              "waterway": ["river", "stream", "canal"], "water": True},
    "trees": {"natural": ["tree", "tree_row"]},
}


def grid_shape(minx, miny, maxx, maxy, res):
    nx = int(math.ceil((maxx - minx) / res))
    ny = int(math.ceil((maxy - miny) / res))
    return nx, ny


def paint(masks, ny, nx):
    city = np.full((ny, nx), ROAD, dtype=np.int8)
    for name, value in (("grass", GRASS), ("wood", TREE), ("water", WATER), ("buildings", BUILDING)):
        if name in masks:
            city[masks[name]] = value
    if "trees" in masks:
        free = (city != BUILDING) & (city != WATER)
        city[masks["trees"] & free] = TREE
    return city


def center_crop(city, n):
    ny, nx = city.shape
    n = min(n, ny, nx)
    j0, i0 = (ny - n) // 2, (nx - n) // 2
    return city[j0:j0 + n, i0:i0 + n]


def percentages(city):
    counts = np.bincount(city.ravel().astype(np.int64), minlength=6)
    return {n: 100.0 * c / city.size for n, c in zip(NAMES, counts)}


def sanity_warnings(pct):
    w = []
    if pct["building"] < 5:
        w.append("Very few building cells (<5%). Usually a coordinate-system problem or an empty download.")
    if pct["road"] > 90:
        w.append("Over 90% of cells are road/paved: the layers probably did not rasterize.")
    if pct["grass"] + pct["tree"] < 1:
        w.append("Almost no grass or trees. Check the park/wood layers.")
    return w


# ======================================================================
# Part B: map data and geometry (needs osmnx, geopandas, rasterio, pyproj)
# ======================================================================
def project_bbox(bbox):
    from pyproj import Transformer
    west, south, east, north = bbox
    tr = Transformer.from_crs("EPSG:4326", CRS_METRIC, always_xy=True)
    xs, ys = zip(*[tr.transform(lon, lat) for lon in (west, east) for lat in (south, north)])
    return min(xs), min(ys), max(xs), max(ys)


def _features(ox, bbox, tags):
    west, south, east, north = bbox
    try:                                    # osmnx 2.x
        return ox.features_from_bbox(bbox=(west, south, east, north), tags=tags)
    except TypeError:                       # osmnx 1.x
        return ox.features_from_bbox(north, south, east, west, tags=tags)


def fetch_osm(bbox, cache_dir):
    import geopandas as gpd
    import osmnx as ox
    os.makedirs(cache_dir, exist_ok=True)
    ox.settings.use_cache = True
    ox.settings.cache_folder = os.path.join(cache_dir, "osmnx")
    key = hashlib.md5(json.dumps(bbox).encode()).hexdigest()[:8]
    layers = {}
    for name, tags in OSM_QUERIES.items():
        gpkg = os.path.join(cache_dir, f"osm_{name}_{key}.gpkg")
        empty = os.path.join(cache_dir, f"osm_{name}_{key}.empty")
        if os.path.exists(gpkg):
            layers[name] = gpd.read_file(gpkg)
            print(f"  {name}: {len(layers[name])} features (cached)")
            continue
        if os.path.exists(empty):
            layers[name] = None
            print(f"  {name}: none (cached)")
            continue
        print(f"  {name}: downloading from OpenStreetMap ...")
        try:
            gdf = _features(ox, bbox, tags)
        except Exception as exc:            # osmnx raises when a query has no results
            print(f"    no features ({type(exc).__name__}: {exc})")
            open(empty, "w").close()
            layers[name] = None
            continue
        gdf = gdf[["geometry"]].reset_index(drop=True)
        gdf.to_file(gpkg, driver="GPKG")
        layers[name] = gdf
        print(f"    {len(gdf)} features")
    return layers


def rasterize_masks(layers, transform, shape, tree_radius_m=3.0, buildings_all_touched=False):
    from rasterio.features import rasterize
    masks = {}
    for name, gdf in layers.items():
        if gdf is None or len(gdf) == 0:
            continue
        geoms = gdf.to_crs(CRS_METRIC).geometry
        geoms = geoms[geoms.notna() & ~geoms.is_empty]
        try:
            geoms = geoms.make_valid()
        except Exception:
            pass
        if name == "water":                 # rivers and streams are lines: give them a width
            geoms = geoms.apply(lambda s: s.buffer(3.0) if "Line" in s.geom_type else s)
        elif name == "trees":               # single trees are points, tree rows are lines
            geoms = geoms.apply(lambda s: s.buffer(tree_radius_m)
                                if ("Point" in s.geom_type or "Line" in s.geom_type) else s)
        shapes = [(g, 1) for g in geoms if g is not None and not g.is_empty]
        if not shapes:
            continue
        touched = name in ("trees", "water") or (name == "buildings" and buildings_all_touched)
        m = rasterize(shapes, out_shape=shape, transform=transform, fill=0,
                      all_touched=touched, dtype="uint8")
        masks[name] = m.astype(bool)
    return masks


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bbox", type=float, nargs=4, metavar=("WEST", "SOUTH", "EAST", "NORTH"),
                    default=[-3.710, 40.406, -3.672, 40.426])
    ap.add_argument("--res", type=float, default=5.0, help="cell size in metres (default 5)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--preview", help="optional PNG preview path")
    ap.add_argument("--sample", type=int, help="also write an N x N centre crop next to --out")
    ap.add_argument("--cache", default="data/raw", help="download cache folder (git-ignored)")
    ap.add_argument("--tree-radius", type=float, default=3.0, help="canopy radius of a mapped tree, metres")
    ap.add_argument("--buildings-all-touched", action="store_true",
                    help="also mark cells that a building only touches (keeps tiny buildings)")
    a = ap.parse_args()

    from rasterio.transform import from_origin
    minx, miny, maxx, maxy = project_bbox(a.bbox)
    nx, ny = grid_shape(minx, miny, maxx, maxy, a.res)
    print(f"grid {nx} x {ny} cells at {a.res} m (about {nx * a.res / 1000:.1f} km x {ny * a.res / 1000:.1f} km)")
    if nx * ny > 60_000_000:
        sys.exit("grid is too large; use a smaller --bbox or a larger --res")

    print("map data:")
    layers = fetch_osm(a.bbox, a.cache)
    transform = from_origin(minx, maxy, a.res, a.res)      # north-west corner; row 0 = north
    masks = rasterize_masks(layers, transform, (ny, nx), a.tree_radius, a.buildings_all_touched)
    city = paint(masks, ny, nx)

    gen_city.write_city(a.out, city)
    pct = percentages(city)
    print(f"wrote {a.out}: {nx}x{ny}, " + ", ".join(f"{k} {v:.1f}%" for k, v in pct.items()))
    for w in sanity_warnings(pct):
        print("WARNING:", w)

    meta = {"bbox_lonlat_west_south_east_north": a.bbox, "crs": CRS_METRIC, "cell_size_m": a.res,
            "origin_nw_corner_x_y": [minx, maxy], "nx": nx, "ny": ny, "row0": "north",
            "layers_used": sorted(masks), "tree_radius_m": a.tree_radius,
            "generated": datetime.date.today().isoformat(), "source": "OpenStreetMap contributors (ODbL)"}
    with open(os.path.splitext(a.out)[0] + ".json", "w") as f:
        json.dump(meta, f, indent=2)

    if a.sample:
        crop = center_crop(city, a.sample)
        path = os.path.splitext(a.out)[0] + f"_sample{crop.shape[1]}.city"
        gen_city.write_city(path, crop)
        print(f"wrote {path}: {crop.shape[1]}x{crop.shape[0]} centre crop")
    if a.preview:
        gen_city.save_preview(a.preview, city)
        print("preview", a.preview)


if __name__ == "__main__":
    main()
