# 3dcm-mexico

Automated creation of **3D city models of Mexican cities from open data**.

This repository contains the code behind the paper *"Creating 3D city models of Mexican cities based on open data"* (K. Arroyo Ohori & J. Stoter). It takes open **INEGI** elevation and topographic data and produces a textured/coloured 3D model (Wavefront OBJ) and a semantically rich one (CityJSON) for an area, including buildings, roads, plant cover, water bodies and terrain.

The work is partly based on [elevador](https://github.com/kenohori/elevador), which itself draws on the methodologies of [3dfier](https://tudelft3d.github.io/3dfier/) and [City4CFD](https://github.com/tudelft3d/city4cfd).

> **Note on the paper.** The published paper (see [Citing](#citing)) describes the implementation as it was at the time of writing. Since then, several steps it describes as manual — road-polygon Boolean operations, DSM−DTM masking, region growing and raster→polygon conversion — have been folded into the C++ tool (see [Roadmap](#roadmap--planned-integration)). This README describes the current code.

---

## Methodology overview

The pipeline is summarised below. Currently the steps marked *manual* are performed in QGIS; the long-term goal is to fold them into the C++ tool so the whole process runs end-to-end.

```
                    ┌─────────────────────────────────────────────────────────┐
                    │                      INEGI open data                     │
                    │  1:50k vector topography     DSM / DTM elevation rasters │
                    └─────────────────────────────────────────────────────────┘
                                       │
                    ┌──────────────────┼───────────────────┐
                    ▼                  ▼                   ▼
                       city blocks +     other polygons          DSM / DTM
                        land use /       (water bodies,          elevation
                        water bdry        plant cover,            rasters
                       (road holes)       terrain from
                                          city blocks)
                    │                  │                   │
                    │                  │                   │
                    └──────────────────┼───────────────────┘
                                       ▼
                    ┌──────────────────────────────────────────┐
                    │   elevadormx (C++)                       │
                    │  • generate road polygons (GEOS)         │
                    │    (union of city blocks → complement)   │
                    │  • extract building footprints           │
                    │    (DSM−DTM mask + region growing)       │
                    │  1. DTM → simplified TIN                 │
                    │  2. repair + triangulate polygons (CGAL) │
                    │  3. lift polygons to 3D                  │
                    │     (flat / vertex / +pts)               │
                    │  4. generate vertical walls              │
                    │  5. write OBJ + CityJSON                 │
                    └──────────────────────────────────────────┘
                                       │
                    ┌──────────────────┴───────────────────┐
                    ▼                                          ▼
             3D model (.obj)                         CityJSON (.city.json)
               for viewing                         with semantics per object
```

### Pipeline stages

1. **Download INEGI data** — the 1:50 000 vector topographic dataset, plus the higher-resolution DSM/DTM rasters available for parts of the country (in this paper, 1.5 m data around Mexico City).
2. **Reorder tiles** (`reorder.py`) — a one-off script that renames/organises downloaded INEGI DTM tiles (strips `conjunto_de_datos`/`metadatos` wrappers, names folders by their 8-character tile code).
3. **Road polygons** *(C++, from `manzana_a`)* — the city blocks from the topography are read and unioned (via GEOS through OGR), along with the water bodies (`--waterbody`) and any land-use features (`--land_use`, comma-separated paths, e.g. INEGI `granja_a`, `ins_deportiv_a`, `cementerio_a`, `area_publica_a`). The complement within the study area (the DSM tile extent, or a custom `study_area`) is taken as the road polygons, so water bodies and land-use areas become holes in the roads. A first approximation classifies the remaining gaps as roads; classification by proximity to the `vialidad_l` line features is planned.
4. **Building footprints** *(C++, except Visvalingam–Whyatt simplification)*:
   - Subtract the DTM from the DSM to get object heights, and mask areas where buildings should not exist (roads, railways, water streams, green areas, water bodies) to NODATA *(C++, `--mask_output`, using the available Road/WaterBody/PlantCover layers)*.
   - Region growing *(C++, `--grow_output`)* from seed points ≥ 10 m, with an adaptive height tolerance (15 m for buildings taller than 100 m, 0.75 m otherwise) and 4-connectivity.
   - Keep only footprints ≥ 45 pixels (~100 m²).
   - Polygonise the labelled raster *(C++, `--buildings_output`)*; with `--grow_output` the resulting footprints are loaded into the model automatically in the same run. Simplification with Visvalingam–Whyatt (tolerance 3 m) is still manual in QGIS.
5. **Preprocessing** *(C++)* — all polygons are repaired and triangulated (constrained Delaunay triangulation + odd-even interior/exterior labelling, per Ledoux et al. 2014). A simplified DTM is built as a TIN from points every 30 m, each set to the median of DTM points within a 120 m radius.
6. **Polygon lifting** *(C++)* — three lifting rules:
   - *Flat:* each building footprint is raised to the 90th percentile of the DSM heights inside it.
   - *Vertices:* road, water body and terrain polygon vertices are interpolated from the DTM TIN.
   - *Vertices + interior points:* plant cover vertices are lifted from the TIN and interior TIN points are added, then the interior is retriangulated.
7. **Vertical walls** *(C++)* — the gaps between differently-lifted polygons are closed (mainly building façades).
8. **Output** *(C++)* — OBJ (with a material file for per-class colours, plus a separate terrain OBJ for debugging) and CityJSON with `Building`, `Road`, `PlantCover` and `WaterBody` semantics.

---

## Repository layout

```
3dcm-mexico/
├── buildinggrower.py          # Reference Python region-growing (superseded by C++ `--grow_output`)
├── reorder.py                 # One-off INEGI DTM tile reorganisation (Python)
├── elevadormx/                # Main C++ tool (Xcode project)
│   └── elevadormx/
│       ├── main.cpp           # Pipeline: TIN building, polygon lifting, walls, OBJ/CityJSON output
│       ├── Quadtree.h                         # Spatial index for the point clouds
│       ├── Edge_map.h                         # Edge adjacency index (for wall/bowtie handling)
│       └── Enhanced_constrained_triangulation_2.h  # CDT with odd-even constraint insertion
└── paper/                     # ISPRS abstract "Creating 3D city models of Mexican cities based on open data"
    ├── ISPRSguidelines_authors_abstract.tex
    └── figures/
```

---

## Dependencies

- **C++20** compiler (e.g. recent Xcode/Clang)
- **GDAL/OGR** — raster & vector I/O
- **CGAL** — constrained Delaunay triangulation, point clouds, geometric predicates
- **nlohmann/json** — CityJSON output
- Python 3 + **rasterio**/**numpy** (only for `buildinggrower.py`)

On macOS with Homebrew: `brew install gdal cgal nlohmann-json`. The Xcode project expects these under `/opt/homebrew`; adjust `HEADER_SEARCH_PATHS` / `LIBRARY_SEARCH_PATHS` if your install differs.

---

## Usage

### C++ tool (`elevadormx`)

Inputs and parameters can be supplied either as command-line arguments or through a JSON config file (see `config.example.json`). A copy of `config.example.json` is the recommended starting point:

```sh
cp config.example.json config.json   # edit the paths to your data
elevadormx --config config.json
```

Alternatively, pass everything on the command line. Command-line options override the config file:

```sh
elevadormx \
  --dsm      .../e14a39b3_ms.bil \
  --dtm      .../e14a39b3_mt.bil \
  --waterbody .../water\ bodies.gpkg \
  --plantcover .../plant\ cover.gpkg \
  --terrain  .../terrain.gpkg \
  --generate_roads true \
  --city_blocks .../manzana_a.shp \
  --land_use .../granja_a.shp,.../ins_deportiv_a.shp,.../cementerio_a.shp \
  --roads_output .../roads.gpkg \
  --study_area 476634,2142300,482533.5,2149281 \
  --terrain_obj  .../terrain.obj \
  --mask_output  .../buildings_masked.tif \
  --grow_output  .../building_labels.tif \
  --buildings_output .../building_footprints.gpkg \
  --obj      .../cdmx.obj \
  --cityjson .../cdmx.city.json
```

Setting both `--grow_output` and `--buildings_output` generates the building footprints and loads them into the model in the same run; `--building` is only needed when footprints are prepared separately (e.g. to reuse a previously generated `.gpkg`).

The two raster paths (`--dsm`, `--dtm`) and the three output paths are required; the vector layers are optional and skipped with a warning if omitted. The full set of recognised options mirrors the keys in `config.example.json`:

| Option | Meaning |
|---|---|
| `--dsm`, `--dtm` | DSM / DTM raster paths (required) |
| `--building`, `--waterbody`, `--plantcover`, `--road`, `--terrain` | Vector layer paths (`--building` is only used when footprints are not generated in-tool) |
| `--generate_roads` | Generate road polygons from city blocks instead of reading `--road` |
| `--city_blocks` | INEGI `manzana_a` layer (city blocks) used for road generation |
| `--land_use` | Comma-separated land-use polygon layers to exclude from roads (e.g. `granja_a`, `ins_deportiv_a`) |
| `--roads_output` | Where to write the generated road polygons (`.gpkg`) |
| `--study_area` | Bounds `x_min,y_min,x_max,y_max` to generate roads within (defaults to the DSM extent) |
| `--terrain_obj`, `--obj`, `--cityjson` | Output paths (required) |
| `--mask_output` | Write the object-height raster (DSM−DTM) with roads/water/green masked to NODATA |
| `--building_mask` | Masked object-height raster to grow buildings from (defaults to `--mask_output` output) |
| `--grow_output` | Write the region-growing building labels (uint32 raster) |
| `--buildings_output` | Write the polygonised building footprints (`.gpkg`) and load them into the model in the same run (when set together with `--grow_output`) |
| `--seed_threshold`, `--tall_building_height`, `--tall_tolerance`, `--normal_tolerance`, `--minimum_region_area` | Region-growing parameters |
| `--dtm_cell_size`, `--dtm_search_radius`, `--dtm_ratio_to_use` | Simplified DTM TIN parameters |
| `--building_height_percentile` | Building height percentile (flat lifting) |
| `--bucket_size`, `--maximum_depth` | Quadtree tuning |
| `--decimal_digits` | Output coordinate precision |

Build in Xcode, then run. Outputs are written to:

- `terrain.obj` — simplified DTM TIN (debug/parameter tuning)
- `buildings_masked.tif` — object heights (DSM−DTM) with roads/water/green masked to NODATA
- `building_labels.tif` — region-growing output (uint32 building id per pixel)
- `building_footprints.gpkg` — polygonised building footprints (loaded into the model automatically)
- `cdmx.obj` — full 3D model for visualisation
- `cdmx.city.json` — CityJSON model with semantics

### Key tunable parameters

| Parameter | Location | Purpose |
|---|---|---|
| `seed_threshold = 10.0` | config / CLI | Minimum object height to seed a building |
| `tall_building_height = 100.0` | config / CLI | Height above which the tall tolerance applies |
| `tall_tolerance = 15.0` / `normal_tolerance = 0.75` | config / CLI | Region-growing height difference (tall vs. normal buildings) |
| `minimum_region_area = 45` | config / CLI | Minimum footprint size in pixels |
| `dtm_cell_size = 30.0` | config / CLI | Grid spacing of the simplified DTM TIN |
| `dtm_search_radius = 120.0` | config / CLI | Radius around each TIN point |
| `dtm_ratio_to_use = 0.5` | config / CLI | Quantile of DTM points used as each TIN point's elevation (0.5 = median) |
| `building_height_percentile = 0.9` | config / CLI | Building height percentile (flat lifting) |
| `bucket_size` / `maximum_depth` | config / CLI | Quadtree tuning |

---

## Known limitations

- Building footprint generation misses roughly 30 % of smaller buildings and can mistake tall vegetation for buildings.
- Terrace-shaped buildings may be split into multiple footprints; adjacent same-height buildings may be merged.
- 3D road structures (overpasses, interchanges) are not modelled — roads are set to DTM height.
- The CityJSON writer stores the terrain under the (non-standard) type `Terrain`.
- The `--mask_output` raster masks the Road/WaterBody/PlantCover layers passed to the tool (the mask is rasterized one geometry per `GDALRasterizeGeometries` call — multi-geometry calls silently drop polygons, see AGENTS.md); railway and water-stream corridors are not yet included.

## Roadmap / planned integration

The following steps are still performed manually in QGIS and are intended to be ported into the C++ tool:

- [x] CLI/configuration-file support (replace hardcoded paths)
- [x] Boolean operations for road-polygon generation (city blocks only)
- [x] DSM−DTM subtraction and NODATA masking of forbidden areas
- [x] Region growing (`buildinggrower.py` → C++, `--grow_output`)
- [x] Include land-use and water features in the road-polygon union
- [ ] Classify road polygons by proximity to `vialidad_l`/`via_ferrea_l` line features
- [x] Raster→polygon conversion (`--buildings_output`)
- [ ] Visvalingam–Whyatt simplification of building footprints

## Citing

If you use this work, please cite the paper:

> Arroyo Ohori, K. and Stoter, J.: Creating 3D city models of Mexican cities based on open data, Int. Arch. Photogramm. Remote Sens. Spatial Inf. Sci., XLVIII-3/W4-2025, 3–9, https://doi.org/10.5194/isprs-archives-XLVIII-3-W4-2025-3-2026, 2026.

[Paper](https://isprs-archives.copernicus.org/articles/XLVIII-3-W4-2025/3/2026/) · [DOI](https://doi.org/10.5194/isprs-archives-XLVIII-3-W4-2025-3-2026)
