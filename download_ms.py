import pandas as pd
import geopandas as gpd
from shapely import geometry
import mercantile
from tqdm import tqdm
import os
import tempfile

# Geometry copied from https://geojson.io
aoi_geom = {
    "coordinates": [
        [
            [-99.23, 19.44],
            [-99.23, 19.37],
            [-99.16, 19.37],
            [-99.16, 19.44],
            [-99.23, 19.44],
        ]
    ],
    "type": "Polygon",
}
aoi_shape = geometry.shape(aoi_geom)
minx, miny, maxx, maxy = aoi_shape.bounds

output_fn = "/Users/ken/Downloads/footprints.geojson"