# Downloading INEGI data

How to obtain the INEGI open data that this project uses, and what is available
for the test area around the 1:50k sheet **E14A39** (Ciudad de México). All
URLs and UPCs were verified on 2026-08-21.

## What the project uses

* **Topographic vectors** — the 1:50 000 "Conjunto de Datos Vectoriales de
  Información Topográfica" (SHP zip, ~29 MB per sheet), extracted to
  `data/topo/<upc>_s/`. The pipeline reads the `manzana_a`, `area_publica_a`,
  `vialidad_l`, `via_ferrea_l`, `corriente_ag_l`, `cuerpo_agua_a`,
  `estanque_a`, `canal_a` and `corriente_ag_a` layers from it.
* **Elevation** — the per-tile "Modelo Digital de Elevaciones" **1.5 m**
  DSM/DTM (BIL zips, 35–57 MB each), installed as
  `data/dsm/<tile>/<tile>_ms.bil` and `data/dtm/<tile>/<tile>_mt.bil`. The
  local copies of E14A39 are exactly these products (the zips extract to the
  same `conjunto_de_datos/` layout). The old "CEM 3.0" per-tile downloads have
  been retired by INEGI; the current replacement is the national **CEM 4.0**
  mosaic (UPC `794551151600`, one 8.9 GB zip) — not used here.

The ready-made downloader is [`scripts/download_neighbours.py`](scripts/download_neighbours.py):
`python3 scripts/download_neighbours.py --jobs 4` fetches the eight surrounding
sheets (~9 GB of zips, cached under `data/downloads/`); re-runs skip anything
already present.

## Sheet grid and subtiles

1:50k sheets are **15′ lat × 20′ lon**. E14A39 spans 99°00′–99°20′W,
19°15′–19°30′N, and its DSM/DTM are split into **24 subtiles** (`a1`–`f4`),
each ~5.9 × 7.0 km at 1.5 m pixel spacing, UTM zone 14N (Mexico ITRF2008).
The subtile code is a 2×2 quadrant within a lettered pair: letters `a`–`f` run
west→east (two subtile columns each, `a`,`b`,`c` in the north half and
`d`,`e`,`f` in the south half), digits `1`–`4` are NW/NE/SW/SE within the pair.
The same naming scheme is used in every sheet, so `a1` is always the
north-west corner of its sheet.

## Surrounding sheets

All eight neighbours exist in the official grid (verified via INEGI's
GeoServer `Div_50K_geog` layer). W = west, E = east in the bounds column.

| Sheet | City | Bounds | Newest vector edition | Topo UPC |
|---|---|---|---|---|
| E14A29 | Cuautitlán | 99°20′W – 99°00′W, 19°30′N – 19°45′N | 2021 | `889463854128` |
| E14A28 | Villa del Carbón | 99°40′W – 99°20′W, 19°30′N – 19°45′N | 2020 | `889463856184` |
| E14A38 | Toluca de Lerdo | 99°40′W – 99°20′W, 19°15′N – 19°30′N | 2019 | `889463831273` |
| E14A48 | Tenango de Arista | 99°40′W – 99°20′W, 19°00′N – 19°15′N | 2019 | `889463833413` |
| E14A49 | Milpa Alta | 99°20′W – 99°00′W, 19°00′N – 19°15′N | 2021 | `889463854142` |
| E14B21 | Texcoco | 99°00′W – 98°40′W, 19°30′N – 19°45′N | 2019 | `889463832416` |
| E14B31 | Chalco | 99°00′W – 98°40′W, 19°15′N – 19°30′N | 2023 | `794551093351` |
| E14B41 | Amecameca | 99°00′W – 98°40′W, 19°00′N – 19°15′N | 2019 | `889463833437` |

Topo zip URL (verified live, HTTP 206):

    https://www.inegi.org.mx/contenidos/productos/prod_serv/contenidos/espanol/bvinegi/productos/geografia/imagen_cartografica/1_50_000/<upc>_s.zip

## Elevation products and URLs

All downloads are zips on `https://www.inegi.org.mx/contenidos/.../imagen_cartografica/`.
The server answers range requests, so `curl -r 0-1024` is a cheap liveness
check. Dead INEGI pages return HTTP 200 with a "Esta liga ya no existe" body —
check the Content-Type (`application/x-zip-compressed` = real file) rather
than the status code.

| Product | Resolution | Path pattern |
|---|---|---|
| DSM ("superficie"), 1.5 m | 1.5 m, ed. 2020/2022 | `1_10_000/lidar/1_5m/Superficie/<upc>_b.zip` (BIL) or `_t.zip` (TIF) |
| DTM ("terreno"), 1.5 m | 1.5 m | `1_10_000/lidar/1_5m/terreno/<upc>_b.zip` |
| DSM/DTM 5 m, 2010–2017 runs | 5 m | `1_10_000/lidar/superficie_ASCII/<upc>_b.zip`, `1_10_000/lidar/Terreno_ASCII/<upc>_b.zip` (also `_as.zip`/`_gr.zip`) |
| LiDAR 5 m, 2011 run | 5 m | `1_10_000/lidar/Superficie_GRID/<upc>_gr.zip` (GRID only) |

The BIL zips extract to `conjunto_de_datos/<tile>_ms.bil` (DSM) or
`<tile>_mt.bil` (DTM) plus `.hdr`, `.prj`, `.stx`, `.aux.xml`, `.ovr`, `.xml`
sidecars, and `metadatos/`.

### 1.5 m availability per sheet

Cell = best available DSM/DTM resolution for that subtile (`—` = no elevation
product at all). `e14a38` (Toluca) is the only sheet with full 1.5 m coverage;
E14A48's southern half has no elevation data at all.

| Subtile | e14a29 | e14a28 | e14a38 | e14a48 | e14a49 | e14b21 | e14b31 | e14b41 |
|---|---|---|---|---|---|---|---|---|
| `a1` | 1.5 | 5 | 1.5 | 1.5 | 1.5 | 5 | 5 | 5 |
| `a2` | 1.5 | 5 | 1.5 | 1.5 | 1.5 | 5 | 5 | 5 |
| `a3` | 1.5 | 5 | 1.5 | 1.5 | 1.5 | 5 | 5 | 5 |
| `a4` | 1.5 | 5 | 1.5 | 1.5 | 1.5 | 5 | 5 | 5 |
| `b1` | 5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 |
| `b2` | 5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 |
| `b3` | 5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 |
| `b4` | 5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 |
| `c1` | 5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 5 | 5 |
| `c2` | 5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 5 | 5 |
| `c3` | 5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 5 | 5 |
| `c4` | 5 | 1.5 | 1.5 | 1.5 | 1.5 | 1.5 | 5 | 5 |
| `d1` | 5 | 1.5 | 1.5 | — | 5 | 5 | 1.5 | 5 |
| `d2` | 5 | 1.5 | 1.5 | — | 5 | 5 | 1.5 | 5 |
| `d3` | 5 | 1.5 | 1.5 | — | 5 | 5 | 1.5 | 5 |
| `d4` | 5 | 1.5 | 1.5 | — | 5 | 5 | 1.5 | 5 |
| `e1` | 1.5 | 5 | 1.5 | — | 5 | 1.5 | 5 | 1.5 |
| `e2` | 1.5 | 5 | 1.5 | — | 5 | 1.5 | 5 | 1.5 |
| `e3` | 1.5 | 5 | 1.5 | — | 5 | 1.5 | 5 | 1.5 |
| `e4` | 1.5 | 5 | 1.5 | — | 5 | 1.5 | 5 | 1.5 |
| `f1` | 5 | 1.5 | 1.5 | — | 5 | 5 | 5 | 5 |
| `f2` | 5 | 1.5 | 1.5 | 5 | 5 | 5 | 5 | 5 |
| `f3` | 5 | 1.5 | 1.5 | — | 5 | 5 | 5 | 5 |
| `f4` | 5 | 1.5 | 1.5 | 5 | 5 | 5 | 5 | 5 |

### 1.5 m DSM/DTM UPCs

All listed tiles have both products; `download_neighbours.py` embeds the same
table. Subtiles not listed only have 5 m (or nothing) — see the 5 m pattern
above for those.

| Tile | DSM (superficie) UPC | DTM (terreno) UPC |
|---|---|---|
| `e14a29a1` | `794551167724` | `794551176429` |
| `e14a29a2` | `794551167731` | `794551176436` |
| `e14a29a3` | `794551167748` | `794551176443` |
| `e14a29a4` | `794551167755` | `794551176450` |
| `e14a29e1` | `794551167762` | `794551176467` |
| `e14a29e2` | `794551167779` | `794551176474` |
| `e14a29e3` | `794551167786` | `794551176481` |
| `e14a29e4` | `794551167793` | `794551176498` |
| `e14a28b1` | `794551133675` | `794551141106` |
| `e14a28b2` | `794551133682` | `794551141113` |
| `e14a28b3` | `794551133699` | `794551141120` |
| `e14a28b4` | `794551133705` | `794551141137` |
| `e14a28c1` | `794551134429` | `794551141854` |
| `e14a28c2` | `794551134436` | `794551141861` |
| `e14a28c3` | `794551134443` | `794551141878` |
| `e14a28c4` | `794551134450` | `794551141885` |
| `e14a28d1` | `794551167649` | `794551176344` |
| `e14a28d2` | `794551167656` | `794551176351` |
| `e14a28d3` | `794551167663` | `794551176368` |
| `e14a28d4` | `794551167670` | `794551176375` |
| `e14a28f1` | `794551167687` | `794551176382` |
| `e14a28f2` | `794551167694` | `794551176399` |
| `e14a28f3` | `794551167700` | `794551176405` |
| `e14a28f4` | `794551167717` | `794551176412` |
| `e14a38a1` | `794551167885` | `794551176580` |
| `e14a38a2` | `794551167892` | `794551176597` |
| `e14a38a3` | `794551167908` | `794551176603` |
| `e14a38a4` | `794551167915` | `794551176610` |
| `e14a38b1` | `794551167922` | `794551176627` |
| `e14a38b2` | `794551167939` | `794551176634` |
| `e14a38b3` | `794551167946` | `794551176641` |
| `e14a38b4` | `794551167953` | `794551176658` |
| `e14a38c1` | `889463842965` | `889463846345` |
| `e14a38c2` | `889463842972` | `889463846352` |
| `e14a38c3` | `889463842989` | `889463846369` |
| `e14a38c4` | `889463842996` | `889463846376` |
| `e14a38d1` | `889463843009` | `889463846383` |
| `e14a38d2` | `889463843016` | `889463846390` |
| `e14a38d3` | `889463843023` | `889463846406` |
| `e14a38d4` | `889463843030` | `889463846413` |
| `e14a38e1` | `889463843047` | `889463846420` |
| `e14a38e2` | `889463843054` | `889463846437` |
| `e14a38e3` | `889463843061` | `889463846444` |
| `e14a38e4` | `889463843078` | `889463846451` |
| `e14a38f1` | `889463843085` | `889463846468` |
| `e14a38f2` | `889463843092` | `889463846475` |
| `e14a38f3` | `889463843108` | `889463846482` |
| `e14a38f4` | `889463843115` | `889463846499` |
| `e14a48a1` | `794551133095` | `794551140529` |
| `e14a48a2` | `794551133101` | `794551140536` |
| `e14a48a3` | `794551133118` | `794551140543` |
| `e14a48a4` | `794551133125` | `794551140550` |
| `e14a48b1` | `794551132623` | `794551140055` |
| `e14a48b2` | `794551132630` | `794551140062` |
| `e14a48b3` | `794551132647` | `794551140079` |
| `e14a48b4` | `794551132654` | `794551140086` |
| `e14a48c1` | `794551132661` | `794551140093` |
| `e14a48c2` | `794551132678` | `794551140109` |
| `e14a48c3` | `794551132685` | `794551140116` |
| `e14a48c4` | `794551132692` | `794551140123` |
| `e14a49a1` | `794551167960` | `794551176665` |
| `e14a49a2` | `794551167977` | `794551176672` |
| `e14a49a3` | `794551167984` | `794551176689` |
| `e14a49a4` | `794551167991` | `794551176696` |
| `e14a49b1` | `889463849995` | `889463851479` |
| `e14a49b2` | `889463850007` | `889463851486` |
| `e14a49b3` | `889463850014` | `889463851493` |
| `e14a49b4` | `889463850021` | `889463851509` |
| `e14a49c1` | `889463850038` | `889463851516` |
| `e14a49c2` | `889463850045` | `889463851523` |
| `e14a49c3` | `889463850052` | `889463851530` |
| `e14a49c4` | `889463850069` | `889463851547` |
| `e14b21b1` | `794551134504` | `794551141939` |
| `e14b21b2` | `794551134511` | `794551141946` |
| `e14b21b3` | `794551134528` | `794551141953` |
| `e14b21b4` | `794551134535` | `794551141960` |
| `e14b21c1` | `794551135266` | `794551142691` |
| `e14b21c2` | `794551135273` | `794551142707` |
| `e14b21c3` | `794551135280` | `794551142714` |
| `e14b21c4` | `794551135297` | `794551142721` |
| `e14b21e1` | `794551168387` | `794551177082` |
| `e14b21e2` | `794551168394` | `794551177099` |
| `e14b21e3` | `794551168400` | `794551177105` |
| `e14b21e4` | `794551168417` | `794551177112` |
| `e14b31b1` | `794551134542` | `794551141977` |
| `e14b31b2` | `794551134559` | `794551141984` |
| `e14b31b3` | `794551134566` | `794551141991` |
| `e14b31b4` | `794551134573` | `794551142004` |
| `e14b31d1` | `794551168431` | `794551177136` |
| `e14b31d2` | `794551168448` | `794551177143` |
| `e14b31d3` | `794551168455` | `794551177150` |
| `e14b31d4` | `794551168462` | `794551177167` |
| `e14b41b1` | `794551135860` | `794551143292` |
| `e14b41b2` | `794551135877` | `794551143308` |
| `e14b41b3` | `794551135884` | `794551143315` |
| `e14b41b4` | `794551135891` | `794551143322` |
| `e14b41e1` | `794551136102` | `794551143537` |
| `e14b41e2` | `794551136119` | `794551143544` |
| `e14b41e3` | `794551136126` | `794551143551` |
| `e14b41e4` | `794551136133` | `794551143568` |

## Topo editions and the schema change

The 2021 update of the topo vector series changed the layer schema. The
**2021+ editions** (E14A39, E14A29, E14A49 2021; E14B31 2023; E14A28 2020)
contain `manzana_a`, `area_publica_a`, `vialidad_l`, `via_ferrea_l` — the
layers the in-tool generators need. The **2019 editions** (E14A38, E14A48,
E14B21, E14B41 — the newest available for those sheets) use the older schema:
names carry a `50` infix (`cuerpo_agua50_a`, `carretera50_l`) and
`manzana_a`/`area_publica_a`/`vialidad_l` do not exist, so
`generate_terrain`/`generate_roads`/`generate_plantcover` cannot run on them.
Newer-schema sheets may also omit layers a sheet simply lacks (no
`corriente_ag_a` in E14A29/E14A49/E14B31; no `canal_a`/`via_ferrea_l` in
E14A28); the tool prints an error and skips a missing `--water_areas` member,
but per-sheet layer paths must still point at files that exist.

## Finding data beyond these eight sheets

* **Sheet codes and bounds**: INEGI GeoServer WFS,
  `https://mapas.inegi.org.mx/geoserver/Sitio_Inegi/ows` with
  `typeName=Sitio_Inegi:Div_50K_geog`, `outputFormat=json` and
  `CQL_FILTER=CLAVE50K='e14a39'` (lowercase). Note: spatial BBOX filters
  currently return no features; filter on the attribute instead.
* **Product UPCs and download URLs**: the Biblioteca digital de Mapas API.
  POST `https://www.inegi.org.mx/app/api/productos/interna_v2/mapas/lista/resultados`
  with a JSON body; the app sends all of these fields (values may be null):
  `{"busc":"","tipoB":1,"adv":false,"malla":"e14a39","buscAG":null,"orden":4,"desc":true,"pag":0,"tam":10}`
  where `malla` is a sheet code and `busc` a free-text/tile search (e.g.
  `"E14A39a1"` for tile-level products). Results carry `key` (UPC), `titulo`,
  `edicion` and `formatos[].url.valor` (relative download URLs). Per-UPC
  details: GET
  `https://www.inegi.org.mx/app/api/productos/interna_v2/ficha/datos?upc=<upc>&lang=es`
  (`info.generales.coordenadas` gives the tile bounds).
* **Product sizes**: topo zips ~29 MB; 1.5 m BIL zips 35–57 MB; a full
  eight-sheet fetch (8 topo + 200 elevation zips) ≈ 9.5 GB.

## Local layout notes

* `data/` is gitignored; the downloader writes zips to `data/downloads/`
  (safe to delete after install) and extracts into `data/topo/`, `data/dsm/`,
  `data/dtm/` in the layout `run_tiles.py` scans.
* A run of `run_tiles.py` over a different sheet needs that sheet's layer
  paths passed via its `--public-areas`, `--city-blocks`, `--road-lines`,
  `--railway-lines`, `--stream-lines` and `--water-areas` arguments.
