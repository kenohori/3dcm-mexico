# AGENTS.md

Guidance for AI agents (and humans) working in this repository.

## What this project is

**3dcm-mexico** automatically creates 3D city models of Mexican cities from open INEGI data. It is the code behind the paper *"Creating 3D city models of Mexican cities based on open data"* (K. Arroyo Ohori & J. Stoter, ISPRS abstract, in `paper/`).

The methodology:
1. INEGI 1:50k topographic vectors (`data/topo/`) provide city blocks, roads (as lines), water bodies, public areas, etc. Building footprints are **not** available — they are extracted from elevation data.
2. Building footprints are extracted from a DSM by region growing (`grow_building_footprints` in `main.cpp`, originally ported from a Python prototype), masked to exclude roads/green/water (`mask_building_areas`).
3. The C++ tool `elevadormx` reads the DSM/DTM rasters + vector layers, builds a simplified DTM TIN, repairs/triangulates polygons, lifts them to 3D (three rules), generates vertical walls, and writes OBJ + CityJSON.

## Repository layout

```
├── CMakeLists.txt             # CMake build (find_package gdal/cgal/nlohmann-json)
├── config.example.json        # Template for the C++ tool's CLI/config interface
├── elevadormx/
│   ├── elevadormx.xcodeproj/  # Xcode project (macOS only)
│   └── elevadormx/
│       ├── main.cpp           # Entire pipeline (single translation unit, ~1700 lines)
│       ├── Quadtree.h         # Spatial index for point clouds
│       ├── Edge_map.h         # Edge adjacency index (currently unused/dead code)
│       └── Enhanced_constrained_triangulation_2.h  # CDT with odd-even constraint insertion
├── paper/                     # ISPRS abstract LaTeX + figures (compiled PDF committed)
└── data/                      # GITIGNORED local source data (INEGI rasters + vectors)
```

## Build & run

**macOS only.** Depends on Homebrew `gdal`, `cgal`, `gmp`, `mpfr` (and `nlohmann-json` headers). `gmp`/`mpfr` are needed by CGAL's exact kernel (used for the constrained triangulations; the road-polygon Boolean operations use GEOS through OGR).

Build with CMake (preferred for non-Xcode workflows):

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Debug
cmake --build build-cmake -j
./build-cmake/elevadormx --config config.example.json
```

Or with Xcode:

```sh
xcodebuild -project elevadormx/elevadormx.xcodeproj -scheme elevadormx \
  -configuration Debug -derivedDataPath build build
./build/Build/Products/Debug/elevadormx --config config.example.json
```

The Xcode project links GDAL via the `libgdal.dylib` symlink in `/opt/homebrew/lib`, so it survives Homebrew upgrades; the CMake build locates the same libraries via `find_package`.

The C++ tool accepts `--key value` CLI args and/or `--config <file.json>` (JSON config overrides defaults; CLI args override the config file). Required: `--dsm`, `--dtm`, `--terrain_obj`, `--obj`, `--cityjson`. See `config.example.json` for the full key list. `main.cpp` prints the effective config on startup and validates required paths before doing any work.

## Coding conventions

- **Single-file architecture**: everything lives in `main.cpp`. Keep it that way until a deliberate refactor; the headers (`Quadtree.h`, `Edge_map.h`, `Enhanced_constrained_triangulation_2.h`) are template-only and owned by Ken Arroyo Ohori (GPL header).
- **C++20** (`gnu++20`), CGAL `Exact_predicates_inexact_constructions_kernel`, GDAL/OGR for I/O, `nlohmann/json` for CityJSON.
- Do **not** add comments unless the surrounding code already has them; match existing style (2-space indent, braces on next line, `for (...)` loops with body on the same line).
- CityJSON output must stay valid: coordinate precision is controlled by `scale_factor` (from `config.decimal_digits`) and the transform `scale` must match the vertex encoding.
- Semantic class names ("Building", "Road", "PlantCover", "WaterBody", "Terrain") are used verbatim as CityJSON types; "Terrain" is non-standard and a known limitation.

## Gotchas & known issues

- **`elevadormx` assumes pre-generated inputs.** It reads building footprints, water bodies, plant cover, and terrain from vector layers (`.gpkg`) — only terrain is still done in Python/QGIS. Building footprints, road polygons, plant cover, and water bodies, however, can now be generated in-tool: roads from the INEGI `manzana_a` city-block layer, optionally subtracting water bodies (`--waterbody`, or the `--water_areas` layers when `--generate_waterbodies`) and land-use layers (`--land_use`) via GEOS Boolean operations (`--generate_roads true`, skipping the `--road` input); the road area is then classified as Road/Railway/WaterBody against the INEGI line layers (`--road_lines`/`vialidad_l`, `--railway_lines`/`via_ferrea_l`, `--stream_lines`/`corriente_ag_l`): the lines are noded into segments with `UnaryUnion` (splitting them at their mutual intersections), each road triangle is classified by its nearest segment (ties go to Road, and triangles beyond `line_classification_distance` stay Road), and adjacent triangles sharing a segment are merged into polygons carrying that segment's class and attributes in the CityJSON object; building footprints by raster→polygon of the region-growing labels (`--buildings_output`), simplified in-tool with Visvalingam–Whyatt (`--simplify_tolerance`, default 3 m). When both `--grow_output` and `--buildings_output` are set, the generated footprints are loaded into the model automatically in the same run and any `--building` input is ignored (a warning is printed if both are given); plant cover from the INEGI `area_publica_a` public-areas layer, clipped to the study area via GEOS `Intersection` (`--generate_plantcover true` + `--public_areas`, skipping the `--plantcover` input; a warning is printed if both are given, and the generated polygons can be written with `--plantcover_output`); water bodies from the INEGI areal water layers `--water_areas` (`cuerpo_agua_a`, `estanque_a`, `canal_a`, `corriente_ag_a`), each clipped to the study area via GEOS `Intersection` (`--generate_waterbodies true`, skipping the `--waterbody` input; a warning is printed if both are given, and the generated polygons can be written with `--waterbody_output`).
- **Road-polygon Booleans use GEOS, not CGAL's exact kernel.** CGAL `Polygon_set_2` crashes with `res != EQUAL` on the shared/partially-collinear boundaries between INEGI water bodies and city blocks; GEOS (via OGR `UnionCascaded`/`Difference`) handles them robustly.
- **`GDALRasterizeGeometries` silently drops polygons when called with many geometries at once.** Observed: only 9 of 54 PlantCover polygons got burned into the building mask, so green areas were not masked and region growing produced buildings inside them. `mask_building_areas` therefore rasterizes the mask **one geometry per call** (`nGeomCount=1`, matching how `gdal_rasterize`/`GDALRasterizeLayers` works). Do not revert to a single multi-geometry call.
- **Do not re-close rings that are already closed** when building OGR geometries in `mask_building_areas`. OGR rings read from a source layer are closed, and re-adding the first point produces a degenerate duplicate closing point that makes some polygons fail to rasterize. Guard the closure with a `back() != front()` check (as the polygon-repair step does).
- **Vector polygon inputs are read within the study area.** `read_polygon_layer` applies the study-area spatial filter (when one is set), so only features intersecting it are loaded into the model. Prepared layers (`plantcover.gpkg`, generated footprints) are already clipped; raw INEGI area layers (e.g. `cuerpo_agua_a`) are not — the filter keeps their out-of-area features (which would otherwise be flattened with garbage z) out of the output.
- **Latent crash/UB paths exist and are being hardened** (empty point clouds, out-of-range percentile indexing, `map.polygons.front()` on empty maps). Always test with missing/bogus paths before trusting a run.
- **Use the tolerant NODATA check when loading rasters.** The INEGI `.bil` files mark NODATA as `-3.4028231e+38` (≈ −FLT_MAX); an exact `value == nodata` comparison leaks those pixels into the point clouds (float→double rounding), producing `-3.4e38` TIN vertices and corrupting z values. Use `is_nodata_value` (relative tolerance) as both `mask_building_areas` and the point-cloud loader now do. Relatedly, `interpolate_dtm_height` clamps points outside the DTM TIN's convex hull to its boundary instead of interpolating on the infinite face (which produced ~1e41 garbage z).
- `Edge_map.h` is included but not instantiated in `main.cpp`; the bowtie wall-handling code in `create_vertical_walls` is commented out and references it.
- `data/` is gitignored (multi-GB local rasters). Do not stage or commit it. `origin/main` may lag local commits.
- The paper's `.tex` must be compiled from `paper/`; figures use relative `figures/...` paths.

## Verification workflow

1. Build with `xcodebuild` (above).
2. Smoke-test CLI: run with no args (expect graceful error), with `--config config.example.json` (expect config printout + graceful missing-file errors), and with a CLI override (must win over the config file).
3. For pipeline changes, run against the real data in `data/` (DSM: `data/dsm/e14a39b3/e14a39b3_ms.bil`, DTM: `data/dtm/e14a39b3/e14a39b3_mt.bil`) and inspect outputs in a viewer.
4. When touching the building-mask pipeline, verify that forbidden areas are actually masked: rasterize the `Road`/`WaterBody`/`PlantCover` polygons and compare with the NODATA pixels of `--mask_output` (should be ~100% covered), and confirm `--grow_output` has no building labels inside green areas.
5. Validate CityJSON with a parser if touching the writer.

## Git conventions

- Commit only when asked. Inspect `git status`/`git diff` first; keep `.DS_Store`, `data/`, build artifacts, and Xcode user state out of commits (`.gitignore` handles these).
- Concise imperative commit messages matching existing history.
