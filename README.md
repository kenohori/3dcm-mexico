# 3dcm-mexico

Automated creation of **3D city models of Mexican cities from open data**.

This repository contains the code behind the paper *"Creating 3D city models of Mexican cities based on open data"* (K. Arroyo Ohori & J. Stoter). It takes open **INEGI** elevation and topographic data and produces a textured/coloured 3D model (Wavefront OBJ) and a semantically rich one (CityJSON) for an area, including buildings, roads, plant cover, water bodies and terrain.

The work is partly based on [elevador](https://github.com/kenohori/elevador), which itself draws on the methodologies of [3dfier](https://tudelft3d.github.io/3dfier/) and City4CFD.

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
            road polygons      building footprints      other polygons
            (Boolean union of  (region growing on       (water bodies, plant
             city blocks →      DSM−DTM masked to       cover, terrain from
             complement)        roads/greens)             city blocks)
                   │                  │                   │
                   │                  │                   │
                   └──────────────────┼───────────────────┘
                                      ▼
                         ┌────────────────────────────┐
                         │   elevadormx (C++)          │
                         │  1. DTM → simplified TIN    │
                         │  2. repair + triangulate    │
                         │     polygons (CGAL)         │
                         │  3. lift polygons to 3D     │
                         │     (flat / vertex / +pts)  │
                         │  4. generate vertical walls │
                         │  5. write OBJ + CityJSON    │
                         └────────────────────────────┘
                                      │
                     ┌────────────────┴────────────────┐
                     ▼                                 ▼
                3D model (.obj)                   CityJSON (.city.json)
                for viewing                    with semantics per object
```

### Pipeline stages

1. **Download INEGI data** — the 1:50 000 vector topographic dataset, plus the higher-resolution DSM/DTM rasters available for parts of the country (in this paper, 1.5 m data around Mexico City).
2. **Reorder tiles** (`reorder.py`) — a one-off script that renames/organises downloaded INEGI DTM tiles (strips `conjunto_de_datos`/`metadatos` wrappers, names folders by their 8-character tile code).
3. **Road polygons** *(manual)* — a Boolean union of city blocks and other areal types from the topography is computed; the complement is mostly roads, and remaining areas are classified by proximity to the linear features (roads/railways/water streams).
4. **Building footprints** *(partly manual)*:
   - Subtract the DTM from the DSM to get object heights.
   - Mask areas where buildings should not exist (roads, railways, water streams, green areas, water bodies) to NODATA.
   - Region growing (`buildinggrower.py`) from seed points ≥ 10 m, with an adaptive height tolerance (15 m for buildings taller than 100 m, 0.75 m otherwise) and 4-connectivity.
   - Keep only footprints ≥ 45 pixels (~100 m²).
   - Polygonise the labelled raster and simplify with Visvalingam–Whyatt (tolerance 3 m) *(manual in QGIS)*.
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
├── buildinggrower.py          # Region-growing building-footprint extraction (Python + Rasterio)
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
  --building .../footprints.gpkg \
  --waterbody .../water\ bodies.gpkg \
  --plantcover .../plant\ cover.gpkg \
  --road     .../roads.gpkg \
  --terrain  .../terrain.gpkg \
  --terrain_obj  .../terrain.obj \
  --obj      .../cdmx.obj \
  --cityjson .../cdmx.city.json
```

The two raster paths (`--dsm`, `--dtm`) and the three output paths are required; the vector layers are optional and skipped with a warning if omitted. The full set of recognised options mirrors the keys in `config.example.json`:

| Option | Meaning |
|---|---|
| `--dsm`, `--dtm` | DSM / DTM raster paths (required) |
| `--building`, `--waterbody`, `--plantcover`, `--road`, `--terrain` | Vector layer paths |
| `--terrain_obj`, `--obj`, `--cityjson` | Output paths (required) |
| `--dtm_cell_size`, `--dtm_search_radius`, `--dtm_ratio_to_use` | Simplified DTM TIN parameters |
| `--building_height_percentile` | Building height percentile (flat lifting) |
| `--bucket_size`, `--maximum_depth` | Quadtree tuning |
| `--decimal_digits` | Output coordinate precision |

Build in Xcode, then run. Outputs are written to:

- `terrain.obj` — simplified DTM TIN (debug/parameter tuning)
- `cdmx.obj` — full 3D model for visualisation
- `cdmx.city.json` — CityJSON model with semantics

### Key tunable parameters

| Parameter | Location | Purpose |
|---|---|---|
| `seed_threshold = 10.0` | `buildinggrower.py` | Minimum object height to seed a building |
| `tolerance` 15.0 / 0.75 | `buildinggrower.py` | Region-growing height difference (tall vs. normal buildings) |
| `minimum_area = 45` | `buildinggrower.py` | Minimum footprint size in pixels |
| `dtm_cell_size = 30.0` | config / CLI | Grid spacing of the simplified DTM TIN |
| `dtm_search_radius = 120.0` | config / CLI | Radius around each TIN point |
| `building_height_percentile = 0.9` | config / CLI | Building height percentile (flat lifting) |
| `bucket_size` / `maximum_depth` | config / CLI | Quadtree tuning |

---

## Known limitations

- Building footprint generation misses roughly 30 % of smaller buildings and can mistake tall vegetation for buildings.
- Terrace-shaped buildings may be split into multiple footprints; adjacent same-height buildings may be merged.
- 3D road structures (overpasses, interchanges) are not modelled — roads are set to DTM height.
- The CityJSON writer stores the terrain under the (non-standard) type `Terrain`.

## Roadmap / planned integration

The following steps are still performed manually in QGIS and are intended to be ported into the C++ tool:

- [x] CLI/configuration-file support (replace hardcoded paths)
- [ ] DSM−DTM subtraction and NODATA masking of forbidden areas
- [ ] Region growing (`buildinggrower.py`) in C++
- [ ] Raster→polygon conversion and Visvalingam–Whyatt simplification
- [ ] Boolean operations for road-polygon generation

## Citing

If you use this work, please cite the paper:

> Arroyo Ohori, K. and Stoter, J. *Creating 3D city models of Mexican cities based on open data*. (ISPRS abstract).
