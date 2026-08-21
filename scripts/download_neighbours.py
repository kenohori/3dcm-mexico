#!/usr/bin/env python3
"""Download INEGI data for the eight 1:50k sheets surrounding E14A39.

For each surrounding sheet this fetches:

* the topographic vector product (``<topo_upc>_s.zip``, extracted to
  ``data/topo/<topo_upc>_s/``), and
* every subtile that has a 1.5 m DSM/DTM product (``..._b.zip`` BIL files,
  installed as ``data/dsm/<tile>/<tile>_ms.bil`` and
  ``data/dtm/<tile>/<tile>_mt.bil``).

The layout matches what ``run_tiles.py`` scans, so a later run of
``scripts/run_tiles.py`` picks the new tiles up automatically. Subtiles
without a 1.5 m product (only 5 m or nothing) are skipped and listed at the
end.

UPCs come from the INEGI Biblioteca digital de Mapas catalog, gathered on
2026-08-21. Zips are cached under ``--zip-dir``; anything already downloaded
or installed is skipped, so re-runs (and re-runs after a partial failure) are
cheap. The zips in ``--zip-dir`` are safe to delete once installed.
"""

import argparse
import os
import shutil
import sys
import time
import urllib.request
import zipfile
from concurrent.futures import ThreadPoolExecutor

BASE_URL = "https://www.inegi.org.mx/contenidos/productos/prod_serv/contenidos/espanol/bvinegi/productos/geografia/imagen_cartografica"

# Sheet code -> name, topo vector UPC, and per-subtile 1.5 m DSM/DTM UPCs.
SHEETS = {
    "e14a29": {
        "name": "Cuautitlán",
        "topo_upc": 889463854128,
        "tiles": {
            "a1": {"dsm": 794551167724, "dtm": 794551176429},
            "a2": {"dsm": 794551167731, "dtm": 794551176436},
            "a3": {"dsm": 794551167748, "dtm": 794551176443},
            "a4": {"dsm": 794551167755, "dtm": 794551176450},
            "e1": {"dsm": 794551167762, "dtm": 794551176467},
            "e2": {"dsm": 794551167779, "dtm": 794551176474},
            "e3": {"dsm": 794551167786, "dtm": 794551176481},
            "e4": {"dsm": 794551167793, "dtm": 794551176498},
        },
    },
    "e14a28": {
        "name": "Villa del Carbón",
        "topo_upc": 889463856184,
        "tiles": {
            "b1": {"dsm": 794551133675, "dtm": 794551141106},
            "b2": {"dsm": 794551133682, "dtm": 794551141113},
            "b3": {"dsm": 794551133699, "dtm": 794551141120},
            "b4": {"dsm": 794551133705, "dtm": 794551141137},
            "c1": {"dsm": 794551134429, "dtm": 794551141854},
            "c2": {"dsm": 794551134436, "dtm": 794551141861},
            "c3": {"dsm": 794551134443, "dtm": 794551141878},
            "c4": {"dsm": 794551134450, "dtm": 794551141885},
            "d1": {"dsm": 794551167649, "dtm": 794551176344},
            "d2": {"dsm": 794551167656, "dtm": 794551176351},
            "d3": {"dsm": 794551167663, "dtm": 794551176368},
            "d4": {"dsm": 794551167670, "dtm": 794551176375},
            "f1": {"dsm": 794551167687, "dtm": 794551176382},
            "f2": {"dsm": 794551167694, "dtm": 794551176399},
            "f3": {"dsm": 794551167700, "dtm": 794551176405},
            "f4": {"dsm": 794551167717, "dtm": 794551176412},
        },
    },
    "e14a38": {
        "name": "Toluca de Lerdo",
        "topo_upc": 889463831273,
        "tiles": {
            "a1": {"dsm": 794551167885, "dtm": 794551176580},
            "a2": {"dsm": 794551167892, "dtm": 794551176597},
            "a3": {"dsm": 794551167908, "dtm": 794551176603},
            "a4": {"dsm": 794551167915, "dtm": 794551176610},
            "b1": {"dsm": 794551167922, "dtm": 794551176627},
            "b2": {"dsm": 794551167939, "dtm": 794551176634},
            "b3": {"dsm": 794551167946, "dtm": 794551176641},
            "b4": {"dsm": 794551167953, "dtm": 794551176658},
            "c1": {"dsm": 889463842965, "dtm": 889463846345},
            "c2": {"dsm": 889463842972, "dtm": 889463846352},
            "c3": {"dsm": 889463842989, "dtm": 889463846369},
            "c4": {"dsm": 889463842996, "dtm": 889463846376},
            "d1": {"dsm": 889463843009, "dtm": 889463846383},
            "d2": {"dsm": 889463843016, "dtm": 889463846390},
            "d3": {"dsm": 889463843023, "dtm": 889463846406},
            "d4": {"dsm": 889463843030, "dtm": 889463846413},
            "e1": {"dsm": 889463843047, "dtm": 889463846420},
            "e2": {"dsm": 889463843054, "dtm": 889463846437},
            "e3": {"dsm": 889463843061, "dtm": 889463846444},
            "e4": {"dsm": 889463843078, "dtm": 889463846451},
            "f1": {"dsm": 889463843085, "dtm": 889463846468},
            "f2": {"dsm": 889463843092, "dtm": 889463846475},
            "f3": {"dsm": 889463843108, "dtm": 889463846482},
            "f4": {"dsm": 889463843115, "dtm": 889463846499},
        },
    },
    "e14a48": {
        "name": "Tenango de Arista",
        "topo_upc": 889463833413,
        "tiles": {
            "a1": {"dsm": 794551133095, "dtm": 794551140529},
            "a2": {"dsm": 794551133101, "dtm": 794551140536},
            "a3": {"dsm": 794551133118, "dtm": 794551140543},
            "a4": {"dsm": 794551133125, "dtm": 794551140550},
            "b1": {"dsm": 794551132623, "dtm": 794551140055},
            "b2": {"dsm": 794551132630, "dtm": 794551140062},
            "b3": {"dsm": 794551132647, "dtm": 794551140079},
            "b4": {"dsm": 794551132654, "dtm": 794551140086},
            "c1": {"dsm": 794551132661, "dtm": 794551140093},
            "c2": {"dsm": 794551132678, "dtm": 794551140109},
            "c3": {"dsm": 794551132685, "dtm": 794551140116},
            "c4": {"dsm": 794551132692, "dtm": 794551140123},
        },
    },
    "e14a49": {
        "name": "Milpa Alta",
        "topo_upc": 889463854142,
        "tiles": {
            "a1": {"dsm": 794551167960, "dtm": 794551176665},
            "a2": {"dsm": 794551167977, "dtm": 794551176672},
            "a3": {"dsm": 794551167984, "dtm": 794551176689},
            "a4": {"dsm": 794551167991, "dtm": 794551176696},
            "b1": {"dsm": 889463849995, "dtm": 889463851479},
            "b2": {"dsm": 889463850007, "dtm": 889463851486},
            "b3": {"dsm": 889463850014, "dtm": 889463851493},
            "b4": {"dsm": 889463850021, "dtm": 889463851509},
            "c1": {"dsm": 889463850038, "dtm": 889463851516},
            "c2": {"dsm": 889463850045, "dtm": 889463851523},
            "c3": {"dsm": 889463850052, "dtm": 889463851530},
            "c4": {"dsm": 889463850069, "dtm": 889463851547},
        },
    },
    "e14b21": {
        "name": "Texcoco",
        "topo_upc": 889463832416,
        "tiles": {
            "b1": {"dsm": 794551134504, "dtm": 794551141939},
            "b2": {"dsm": 794551134511, "dtm": 794551141946},
            "b3": {"dsm": 794551134528, "dtm": 794551141953},
            "b4": {"dsm": 794551134535, "dtm": 794551141960},
            "c1": {"dsm": 794551135266, "dtm": 794551142691},
            "c2": {"dsm": 794551135273, "dtm": 794551142707},
            "c3": {"dsm": 794551135280, "dtm": 794551142714},
            "c4": {"dsm": 794551135297, "dtm": 794551142721},
            "e1": {"dsm": 794551168387, "dtm": 794551177082},
            "e2": {"dsm": 794551168394, "dtm": 794551177099},
            "e3": {"dsm": 794551168400, "dtm": 794551177105},
            "e4": {"dsm": 794551168417, "dtm": 794551177112},
        },
    },
    "e14b31": {
        "name": "Chalco",
        "topo_upc": 794551093351,
        "tiles": {
            "b1": {"dsm": 794551134542, "dtm": 794551141977},
            "b2": {"dsm": 794551134559, "dtm": 794551141984},
            "b3": {"dsm": 794551134566, "dtm": 794551141991},
            "b4": {"dsm": 794551134573, "dtm": 794551142004},
            "d1": {"dsm": 794551168431, "dtm": 794551177136},
            "d2": {"dsm": 794551168448, "dtm": 794551177143},
            "d3": {"dsm": 794551168455, "dtm": 794551177150},
            "d4": {"dsm": 794551168462, "dtm": 794551177167},
        },
    },
    "e14b41": {
        "name": "Amecameca",
        "topo_upc": 889463833437,
        "tiles": {
            "b1": {"dsm": 794551135860, "dtm": 794551143292},
            "b2": {"dsm": 794551135877, "dtm": 794551143308},
            "b3": {"dsm": 794551135884, "dtm": 794551143315},
            "b4": {"dsm": 794551135891, "dtm": 794551143322},
            "e1": {"dsm": 794551136102, "dtm": 794551143537},
            "e2": {"dsm": 794551136119, "dtm": 794551143544},
            "e3": {"dsm": 794551136126, "dtm": 794551143551},
            "e4": {"dsm": 794551136133, "dtm": 794551143568},
        },
    },
}

ALL_SUBTILES = [f"{a}{b}" for a in "abcdef" for b in "1234"]


def topo_url(upc):
    return f"{BASE_URL}/1_50_000/{upc}_s.zip"


def elev_url(upc, kind):
    folder = "Superficie" if kind == "dsm" else "terreno"
    return f"{BASE_URL}/1_10_000/lidar/1_5m/{folder}/{upc}_b.zip"


def download(url, dest):
    if os.path.isfile(dest):
        return True
    part = dest + ".part"
    request = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    last_error = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=600) as resp, open(part, "wb") as out:
                shutil.copyfileobj(resp, out)
            os.replace(part, dest)
            return True
        except Exception as err:
            last_error = err
            time.sleep(2 * (attempt + 1))
    print(f"    download failed: {url} ({last_error})", file=sys.stderr)
    return False


def safe_rel(name):
    return not name.startswith("/") and ".." not in name.split("/")


def extract(zip_path, target, members):
    os.makedirs(target, exist_ok=True)
    with zipfile.ZipFile(zip_path) as z:
        for name in z.namelist():
            norm = name.replace("\\", "/")
            if norm.endswith("/") or not safe_rel(norm):
                continue
            rel = members(norm)
            if rel is None:
                continue
            dst = os.path.join(target, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with z.open(name) as src, open(dst, "wb") as out:
                shutil.copyfileobj(src, out)


def install_topo(zip_path, upc, topo_dir):
    target = os.path.join(topo_dir, f"{upc}_s")
    if os.path.isdir(target) and os.listdir(target):
        return True
    extract(zip_path, target, lambda norm: norm)
    return True


def install_elev(zip_path, tile, kind, dsm_dir, dtm_dir):
    target = os.path.join(dsm_dir if kind == "dsm" else dtm_dir, tile)
    if os.path.isdir(target) and any(n.endswith(".bil") for n in os.listdir(target)):
        return True
    prefix = "conjunto_de_datos/"
    extract(zip_path, target, lambda norm: norm[len(prefix):] if norm.startswith(prefix) else None)
    return True


def fetch_topo(sheet, args):
    upc = SHEETS[sheet]["topo_upc"]
    url = topo_url(upc)
    zip_path = os.path.join(args.zip_dir, os.path.basename(url))
    if not download(url, zip_path):
        return (sheet, "topo", False, "download failed")
    install_topo(zip_path, upc, args.topo_dir)
    if not os.path.isfile(os.path.join(args.topo_dir, f"{upc}_s", "conjunto_de_datos", "manzana_a.shp")):
        print(f"    note: {sheet} topo product predates the 2021 schema (no manzana_a); the "
              "in-tool terrain/road/plantcover generators need a 2021+ edition", file=sys.stderr)
    return (sheet, "topo", True, None)


def fetch_elev(sheet, sub, kind, args):
    upc = SHEETS[sheet]["tiles"][sub][kind]
    url = elev_url(upc, kind)
    zip_path = os.path.join(args.zip_dir, os.path.basename(url))
    if not download(url, zip_path):
        return (f"{sheet}{sub}", kind, False, "download failed")
    install_elev(zip_path, f"{sheet}{sub}", kind, args.dsm_dir, args.dtm_dir)
    return (f"{sheet}{sub}", kind, True, None)


def main():
    parser = argparse.ArgumentParser(description="Download INEGI topo vectors and 1.5 m DSM/DTM tiles for the sheets around E14A39")
    parser.add_argument("--sheets", default=",".join(SHEETS), help="Comma-separated sheet codes to process (default: all eight)")
    parser.add_argument("--tiles", default=None, help="Comma-separated subtile codes to process, e.g. a1,b2 (default: all with a 1.5 m product)")
    parser.add_argument("--dsm-dir", default="data/dsm")
    parser.add_argument("--dtm-dir", default="data/dtm")
    parser.add_argument("--topo-dir", default="data/topo")
    parser.add_argument("--zip-dir", default="data/downloads", help="Cache for the downloaded zips")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--dry-run", action="store_true", help="Print what would be fetched without downloading")
    parser.add_argument("--skip-topo", action="store_true")
    parser.add_argument("--skip-elev", action="store_true")
    args = parser.parse_args()

    sheets = [s for s in args.sheets.split(",") if s in SHEETS]
    unknown = [s for s in args.sheets.split(",") if s not in SHEETS]
    if unknown:
        print(f"Unknown sheets: {unknown}", file=sys.stderr)
        sys.exit(1)
    tiles = args.tiles.split(",") if args.tiles else None

    jobs = []
    if not args.skip_topo:
        for sheet in sheets:
            jobs.append((sheet, "topo", None))
    if not args.skip_elev:
        for sheet in sheets:
            for sub in sorted(SHEETS[sheet]["tiles"]):
                if tiles and sub not in tiles:
                    continue
                for kind in ("dsm", "dtm"):
                    jobs.append((sheet, kind, sub))

    n_elev = sum(1 for _, kind, _ in jobs if kind != "topo")
    n_topo = len(jobs) - n_elev
    print(f"Plan: {n_topo} topo zips + {n_elev} 1.5 m DSM/DTM zips for {len(sheets)} sheet(s)")
    if args.dry_run:
        for sheet in sheets:
            s = SHEETS[sheet]
            print(f"  {sheet} ({s['name']}): topo {s['topo_upc']}")
            if not args.skip_elev:
                for sub in sorted(s["tiles"]):
                    if tiles and sub not in tiles:
                        continue
                    print(f"    {sheet}{sub}: dsm {s['tiles'][sub]['dsm']}, dtm {s['tiles'][sub]['dtm']}")
        return

    os.makedirs(args.zip_dir, exist_ok=True)
    results = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {}
        for sheet, kind, sub in jobs:
            if kind == "topo":
                futures[executor.submit(fetch_topo, sheet, args)] = (sheet, "topo")
            else:
                futures[executor.submit(fetch_elev, sheet, sub, kind, args)] = (f"{sheet}{sub}", kind)
        for future, (label, kind) in futures.items():
            label, kind, ok, err = future.result()
            status = "OK" if ok else f"FAILED ({err})"
            print(f"[{label}/{kind}] {status}", flush=True)
            results.append((label, ok))

    failed = [label for label, ok in results if not ok]
    if failed:
        print(f"Failed: {failed}", file=sys.stderr)
        sys.exit(1)

    for sheet in sheets:
        missing = sorted(set(ALL_SUBTILES) - set(SHEETS[sheet]["tiles"]))
        if missing:
            print(f"note: {sheet} ({SHEETS[sheet]['name']}) has no 1.5 m product for subtiles "
                  f"{', '.join(missing)}; they were skipped")


if __name__ == "__main__":
    main()
