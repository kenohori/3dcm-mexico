#!/usr/bin/env python3
"""Batch-run elevadormx over the DSM/DTM tiles of one INEGI sheet.

Each DSM/DTM tile gets its own CityJSON/OBJ, with a non-overlapping study
area computed from the tile grid. Interior seams are placed at the midpoint
of the overlap band shared by each adjacent tile pair; outer edges use each
tile's own extent.
"""

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor


class Tile:
    def __init__(self, tile_id, x_min, y_min, x_max, y_max):
        self.tile_id = tile_id
        self.x_min = x_min
        self.y_min = y_min
        self.x_max = x_max
        self.y_max = y_max
        self.row = None
        self.col = None


def read_hdr_extent(hdr_path):
    meta = {}
    with open(hdr_path) as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                meta[parts[0]] = parts[1]
    x0 = float(meta["ULXMAP"])
    y1 = float(meta["ULYMAP"])
    ncols = int(meta["NCOLS"])
    nrows = int(meta["NROWS"])
    xsize = float(meta["XDIM"])
    ysize = float(meta["YDIM"])
    return x0, y1 - nrows * ysize, x0 + ncols * xsize, y1


def cluster(values, tolerance):
    clusters = []
    for v in sorted(values):
        for cluster in clusters:
            if abs(v - cluster["center"]) < tolerance:
                cluster["center"] = (cluster["center"] * cluster["count"] + v) / (cluster["count"] + 1)
                cluster["count"] += 1
                break
        else:
            clusters.append({"center": v, "count": 1})
    return [c["center"] for c in clusters]


def tile_grid(tiles):
    y_centers = cluster([(t.y_min + t.y_max) / 2 for t in tiles], 4000)
    x_centers = cluster([(t.x_min + t.x_max) / 2 for t in tiles], 4000)
    rows = sorted(y_centers, reverse=True)
    cols = sorted(x_centers)

    for t in tiles:
        t.row = rows.index(min(rows, key=lambda r: abs(r - (t.y_min + t.y_max) / 2)))
        t.col = cols.index(min(cols, key=lambda c: abs(c - (t.x_min + t.x_max) / 2)))

    n_rows = len(rows)
    n_cols = len(cols)
    grid = [[None] * n_cols for _ in range(n_rows)]
    for t in tiles:
        grid[t.row][t.col] = t

    # Vertical seams between adjacent columns: midpoint of the overlap band
    # shared by every tile pair across all rows.
    vseams = []
    for c in range(n_cols - 1):
        max_west = -math.inf
        min_east = math.inf
        for r in range(n_rows):
            left = grid[r][c]
            right = grid[r][c + 1]
            if left is None or right is None:
                continue
            max_west = max(max_west, right.x_min)
            min_east = min(min_east, left.x_max)
        if max_west > min_east:
            raise RuntimeError(f"Column seam {c} has no valid overlap band")
        vseams.append((max_west + min_east) / 2)

    # Horizontal seams between adjacent rows. The seam must be at or above every
    # north tile's south edge (so its cell stays inside the north tile) and at
    # or below every south tile's north edge (so its cell stays inside the south
    # tile).
    hseams = []
    for r in range(n_rows - 1):
        min_south = math.inf
        max_north = -math.inf
        for c in range(n_cols):
            north = grid[r][c]
            south = grid[r + 1][c]
            if north is None or south is None:
                continue
            max_north = max(max_north, north.y_min)
            min_south = min(min_south, south.y_max)
        if max_north > min_south:
            raise RuntimeError(f"Row seam {r} has no valid overlap band")
        hseams.append((max_north + min_south) / 2)

    # Assign each tile a non-overlapping study area.
    for t in tiles:
        t.study_x_min = vseams[t.col - 1] if t.col > 0 else t.x_min
        t.study_x_max = vseams[t.col] if t.col < n_cols - 1 else t.x_max
        t.study_y_min = hseams[t.row] if t.row < n_rows - 1 else t.y_min
        t.study_y_max = hseams[t.row - 1] if t.row > 0 else t.y_max

    return grid, n_rows, n_cols


def main():
    parser = argparse.ArgumentParser(description="Batch-run elevadormx over DSM/DTM tiles")
    parser.add_argument("--dsm-dir", default="data/dsm", help="Directory containing DSM tile folders")
    parser.add_argument("--dtm-dir", default="data/dtm", help="Directory containing DTM tile folders")
    parser.add_argument("--out-dir", default="data/output/tiles", help="Directory for per-tile outputs")
    parser.add_argument("--config-dir", default=None, help="Directory for generated per-tile configs (defaults to --out-dir)")
    parser.add_argument("--binary", default="build-cmake-release/elevadormx", help="Path to the elevadormx binary")
    parser.add_argument("--public-areas", default="data/topo/889463854135_s/conjunto_de_datos/area_publica_a.shp")
    parser.add_argument("--city-blocks", default="data/topo/889463854135_s/conjunto_de_datos/manzana_a.shp")
    parser.add_argument("--road-lines", default="data/topo/889463854135_s/conjunto_de_datos/vialidad_l.shp")
    parser.add_argument("--railway-lines", default="data/topo/889463854135_s/conjunto_de_datos/via_ferrea_l.shp")
    parser.add_argument("--stream-lines", default="data/topo/889463854135_s/conjunto_de_datos/corriente_ag_l.shp")
    parser.add_argument("--water-areas", default="data/topo/889463854135_s/conjunto_de_datos/cuerpo_agua_a.shp,data/topo/889463854135_s/conjunto_de_datos/estanque_a.shp,data/topo/889463854135_s/conjunto_de_datos/canal_a.shp,data/topo/889463854135_s/conjunto_de_datos/corriente_ag_a.shp")
    parser.add_argument("--jobs", type=int, default=2, help="Number of parallel runs")
    parser.add_argument("--tile", default=None, help="Only run this specific tile id")
    parser.add_argument("--generate-configs-only", action="store_true", help="Write configs but do not run")
    args = parser.parse_args()

    tiles = []
    for tile_id in sorted(os.listdir(args.dsm_dir)):
        dsm_hdr = os.path.join(args.dsm_dir, tile_id, f"{tile_id}_ms.hdr")
        if not os.path.isfile(dsm_hdr):
            continue
        x_min, y_min, x_max, y_max = read_hdr_extent(dsm_hdr)
        tiles.append(Tile(tile_id, x_min, y_min, x_max, y_max))

    if not tiles:
        print("No DSM tiles found", file=sys.stderr)
        sys.exit(1)

    grid, n_rows, n_cols = tile_grid(tiles)
    print(f"Grid: {n_rows} rows x {n_cols} cols = {len(tiles)} tiles")

    config_dir = args.config_dir or os.path.join(args.out_dir, "configs")
    os.makedirs(config_dir, exist_ok=True)

    configs = []
    for t in sorted(tiles, key=lambda t: (t.row, t.col)):
        # Final products live directly in the tile folder; intermediates go in work/<tile>
        tile_out = os.path.join(args.out_dir, t.tile_id)
        work_out = os.path.join(args.out_dir, "work", t.tile_id)
        os.makedirs(tile_out, exist_ok=True)
        os.makedirs(work_out, exist_ok=True)
        config = {
            "dsm": os.path.join(args.dsm_dir, t.tile_id, f"{t.tile_id}_ms.bil"),
            "dtm": os.path.join(args.dtm_dir, t.tile_id, f"{t.tile_id}_mt.bil"),
            "generate_plantcover": True,
            "public_areas": args.public_areas,
            "plantcover_output": os.path.join(work_out, "plantcover.gpkg"),
            "generate_waterbodies": True,
            "water_areas": args.water_areas,
            "waterbody_output": os.path.join(work_out, "waterbodies.gpkg"),
            "generate_terrain": True,
            "terrain_output": os.path.join(work_out, "terrain.gpkg"),
            "generate_roads": True,
            "city_blocks": args.city_blocks,
            "road_lines": args.road_lines,
            "railway_lines": args.railway_lines,
            "stream_lines": args.stream_lines,
            "roads_output": os.path.join(work_out, "roads.gpkg"),
            "study_area": f"{t.study_x_min},{t.study_y_min},{t.study_x_max},{t.study_y_max}",
            "terrain_obj": os.path.join(tile_out, "terrain.obj"),
            "obj": os.path.join(tile_out, f"{t.tile_id}.obj"),
            "cityjson": os.path.join(tile_out, f"{t.tile_id}.city.json"),
            "mask_output": os.path.join(work_out, "buildings_masked.tif"),
            "grow_output": os.path.join(work_out, "building_labels.tif"),
            "buildings_output": os.path.join(work_out, "building_footprints.gpkg"),
            "line_classification_distance": 50.0,
        }
        config_path = os.path.join(config_dir, f"{t.tile_id}.config.json")
        with open(config_path, "w") as f:
            json.dump(config, f, indent=2)
        configs.append((t, config_path))
        print(f"  {t.tile_id}: study area {t.study_x_min:.1f},{t.study_y_min:.1f},{t.study_x_max:.1f},{t.study_y_max:.1f}")

    if args.generate_configs_only:
        return

    if args.tile:
        configs = [(t, p) for (t, p) in configs if t.tile_id == args.tile]

    def run_job(job):
        t, config_path = job
        return subprocess.run(
            [args.binary, "--config", config_path],
            capture_output=False,
        ).returncode

    failed = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(run_job, job): job for job in configs}
        for future, (t, _) in futures.items():
            rc = future.result()
            status = "OK" if rc == 0 else f"FAILED (rc={rc})"
            print(f"[{t.tile_id}] {status}", flush=True)
            if rc != 0:
                failed.append(t.tile_id)

    if failed:
        print(f"Failed tiles: {failed}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
