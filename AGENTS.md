# AGENTS.md

Guidance for AI agents (and humans) working in this repository.

## What this project is

**3dcm-mexico** automatically creates 3D city models of Mexican cities from open INEGI data. It is the code behind the paper *"Creating 3D city models of Mexican cities based on open data"* (K. Arroyo Ohori & J. Stoter, ISPRS abstract, in `paper/`).

The methodology:
1. INEGI 1:50k topographic vectors (`data/topo/`) provide city blocks, roads (as lines), water bodies, public areas, etc. Building footprints are **not** available — they are extracted from elevation data.
2. Building footprints are extracted from a DSM by region growing (`buildinggrower.py`, currently Python), masked to exclude roads/green/water.
3. The C++ tool `elevadormx` reads the DSM/DTM rasters + vector layers, builds a simplified DTM TIN, repairs/triangulates polygons, lifts them to 3D (three rules), generates vertical walls, and writes OBJ + CityJSON.

## Repository layout

```
├── buildinggrower.py          # Region-growing building footprint extraction (Python, Rasterio)
├── reorder.py                 # One-off INEGI DTM tile reorganisation (Python, historical)
├── config.example.json        # Template for the C++ tool's CLI/config interface
├── elevadormx/
│   ├── elevadormx.xcodeproj/  # Xcode project (macOS only)
│   └── elevadormx/
│       ├── main.cpp           # Entire pipeline (single translation unit, ~1070 lines)
│       ├── Quadtree.h         # Spatial index for point clouds
│       ├── Edge_map.h         # Edge adjacency index (currently unused/dead code)
│       └── Enhanced_constrained_triangulation_2.h  # CDT with odd-even constraint insertion
├── paper/                     # ISPRS abstract LaTeX + figures (compiled PDF committed)
└── data/                      # GITIGNORED local source data (INEGI rasters + vectors)
```

## Build & run

**macOS + Xcode only.** Depends on Homebrew `gdal`, `cgal`, `gmp` (and `nlohmann-json` headers). The Xcode project links GDAL via the `libgdal.dylib` symlink in `/opt/homebrew/lib`, so it survives Homebrew upgrades.

```sh
xcodebuild -project elevadormx/elevadormx.xcodeproj -scheme elevadormx \
  -configuration Debug -derivedDataPath build build
./build/Build/Products/Debug/elevadormx --config config.example.json
```

Python scripts need `rasterio`, `numpy` (and optionally `geopandas`/`fiona` for data inspection).

The C++ tool accepts `--key value` CLI args and/or `--config <file.json>` (JSON config overrides defaults; CLI args override the config file). Required: `--dsm`, `--dtm`, `--terrain_obj`, `--obj`, `--cityjson`. See `config.example.json` for the full key list. `main.cpp` prints the effective config on startup and validates required paths before doing any work.

## Coding conventions

- **Single-file architecture**: everything lives in `main.cpp`. Keep it that way until a deliberate refactor; the headers (`Quadtree.h`, `Edge_map.h`, `Enhanced_constrained_triangulation_2.h`) are template-only and owned by Ken Arroyo Ohori (GPL header).
- **C++20** (`gnu++20`), CGAL `Exact_predicates_inexact_constructions_kernel`, GDAL/OGR for I/O, `nlohmann/json` for CityJSON.
- Do **not** add comments unless the surrounding code already has them; match existing style (2-space indent, braces on next line, `for (...)` loops with body on the same line).
- CityJSON output must stay valid: coordinate precision is controlled by `scale_factor` (from `config.decimal_digits`) and the transform `scale` must match the vertex encoding.
- Semantic class names ("Building", "Road", "PlantCover", "WaterBody", "Terrain") are used verbatim as CityJSON types; "Terrain" is non-standard and a known limitation.

## Gotchas & known issues

- **`elevadormx` assumes pre-generated inputs.** It reads building footprints, roads, water bodies, plant cover, and terrain from vector layers (`.gpkg`) — it does **not** yet generate footprints or road polygons itself. Those steps are still done in Python/QGIS and are the target of ongoing integration work.
- **Latent crash/UB paths exist and are being hardened** (empty point clouds, out-of-range percentile indexing, `map.polygons.front()` on empty maps). Always test with missing/bogus paths before trusting a run.
- `Edge_map.h` is included but not instantiated in `main.cpp`; the bowtie wall-handling code in `create_vertical_walls` is commented out and references it.
- `data/` is gitignored (multi-GB local rasters). Do not stage or commit it. `origin/main` may lag local commits.
- The paper's `.tex` must be compiled from `paper/`; figures use relative `figures/...` paths.

## Verification workflow

1. Build with `xcodebuild` (above).
2. Smoke-test CLI: run with no args (expect graceful error), with `--config config.example.json` (expect config printout + graceful missing-file errors), and with a CLI override (must win over the config file).
3. For pipeline changes, run against the real data in `data/` (DSM: `data/dsm/e14a39b3/e14a39b3_ms.bil`, DTM: `data/dtm/e14a39b3/e14a39b3_mt.bil`) and inspect outputs in a viewer.
4. Validate CityJSON with a parser if touching the writer.

## Git conventions

- Commit only when asked. Inspect `git status`/`git diff` first; keep `.DS_Store`, `data/`, build artifacts, and Xcode user state out of commits (`.gitignore` handles these).
- Concise imperative commit messages matching existing history.
