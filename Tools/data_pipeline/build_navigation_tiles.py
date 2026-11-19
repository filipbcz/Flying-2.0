"""Render the acquired regional layers into a self-contained raster MBTiles map."""

from __future__ import annotations

import math
import sqlite3
from contextlib import closing
from pathlib import Path

import numpy as np
import pyproj
from rasterio.features import rasterize
from rasterio.io import MemoryFile
from rasterio.transform import from_bounds

from acquire_sources import atomic_json, digest_file, read_json, require


LAYERS = ("zabaged-forest", "zabaged-water", "zabaged-roads", "geonames-settlements",
          "power-lines", "power-masts", "airspaces", "runways", "airports")
COLORS = {0: (246, 243, 227, 255), 1: (160, 193, 144, 255), 2: (121, 184, 224, 255),
          3: (115, 104, 90, 255), 4: (174, 151, 134, 255), 5: (161, 94, 161, 255),
          6: (161, 94, 161, 255), 7: (52, 105, 191, 255), 8: (42, 42, 42, 255),
          9: (200, 47, 44, 255), 255: (0, 0, 0, 0)}
WORLD = 20037508.342789244


def tile_number(longitude, latitude, zoom):
    count = 2 ** zoom
    return ((longitude + 180) / 360 * count,
            (1 - math.asinh(math.tan(math.radians(latitude))) / math.pi) / 2 * count)


def transform_coordinates(coordinates, transformer):
    if isinstance(coordinates[0], (int, float)):
        return list(transformer.transform(*coordinates[:2]))
    return [transform_coordinates(child, transformer) for child in coordinates]


def projected_layers(navigation):
    shapes = []
    counts = {}
    for color, name in enumerate(LAYERS, 1):
        source = read_json(navigation / f"{name}.geojson")
        transformer = pyproj.Transformer.from_crs(5514 if name.startswith(("zabaged-", "geonames-")) else 4326,
                                                  3857, always_xy=True)
        counts[name] = len(source["features"])
        for entry in source["features"]:
            geometry = entry["geometry"]
            require(geometry and geometry["type"] != "GeometryCollection", f"Unsupported geometry in {name}")
            coordinates = transform_coordinates(geometry["coordinates"], transformer)
            kind = geometry["type"]
            if name == "airspaces" and kind == "Polygon":
                kind = "MultiLineString"
            shapes.append((dict(type=kind, coordinates=coordinates), color))
    return shapes, counts


def build_tiles(navigation: Path, bounds: dict, minimum_zoom=12, maximum_zoom=16):
    shapes, counts = projected_layers(navigation)
    archive = navigation / "navigation.mbtiles"
    archive.unlink(missing_ok=True)
    transformer = pyproj.Transformer.from_crs(4326, 3857, always_xy=True)
    west, south = transformer.transform(bounds["minLonDeg"], bounds["minLatDeg"])
    east, north = transformer.transform(bounds["maxLonDeg"], bounds["maxLatDeg"])
    region = dict(type="Polygon", coordinates=[[[west, south], [east, south], [east, north], [west, north], [west, south]]])
    geographic = [bounds[key] for key in ("minLonDeg", "minLatDeg", "maxLonDeg", "maxLatDeg")]
    total = 0
    with closing(sqlite3.connect(archive)) as database, database:
        database.executescript("CREATE TABLE metadata (name TEXT PRIMARY KEY, value TEXT); CREATE TABLE tiles (zoom_level INTEGER, tile_column INTEGER, tile_row INTEGER, tile_data BLOB, PRIMARY KEY(zoom_level,tile_column,tile_row));")
        metadata = dict(name="Ceska Trebova private offline navigation", type="baselayer", version="1.3", format="png",
                        bounds=",".join(map(str, geographic)), minzoom=str(minimum_zoom), maxzoom=str(maximum_zoom),
                        attribution="CUZK ZABAGED/Geonames; Czech AIM; Aeroklub Ceska Trebova. Private simulator only; unverified SLZ; advisory airspace, not an operational chart.")
        database.executemany("INSERT INTO metadata VALUES (?, ?)", metadata.items())
        for zoom in range(minimum_zoom, maximum_zoom + 1):
            first_column, first_row = tile_number(geographic[0], geographic[3], zoom)
            last_column, last_row = tile_number(geographic[2], geographic[1], zoom)
            require((int(last_column) - int(first_column) + 1) * (int(last_row) - int(first_row) + 1) <= 1500, "Unbounded navigation tile request")
            span = WORLD * 2 / 2 ** zoom
            for column in range(int(first_column), int(last_column) + 1):
                for row in range(int(first_row), int(last_row) + 1):
                    left, top = -WORLD + column * span, WORLD - row * span
                    transform = from_bounds(left, top - span, left + span, top, 256, 256)
                    pixels = rasterize(shapes, out_shape=(256, 256), fill=0, transform=transform, all_touched=True, dtype="uint8")
                    mask = rasterize([(region, 1)], out_shape=(256, 256), transform=transform, dtype="uint8")
                    pixels[mask == 0] = 255
                    with MemoryFile() as memory:
                        with memory.open(driver="PNG", width=256, height=256, count=1, dtype="uint8") as image:
                            image.write(pixels, 1)
                            image.write_colormap(1, COLORS)
                        data = memory.read()
                    database.execute("INSERT INTO tiles VALUES (?, ?, ?, ?)", (zoom, column, 2 ** zoom - 1 - row, data))
                    total += 1
    atomic_json(navigation / "style.json", dict(version=8, name="Private regional navigation",
        sources=dict(regional=dict(type="raster", url="mbtiles://navigation.mbtiles", tileSize=256,
                                  minzoom=minimum_zoom, maxzoom=maximum_zoom, bounds=geographic)),
        layers=[dict(id="regional-navigation", type="raster", source="regional")]))
    return dict(tileArchive=dict(path=archive.name, format="mbtiles", tileFormat="png", scheme="tms",
                                sha256=digest_file(archive), tileCount=total, minzoom=minimum_zoom, maxzoom=maximum_zoom),
                style=dict(path="style.json", sha256=digest_file(navigation / "style.json")),
                renderedFeatureCounts=counts, legend={name: COLORS[index][:3] for index, name in enumerate(LAYERS, 1)})


def verify_tiles(navigation, manifest):
    archive = navigation / manifest["tileArchive"]["path"]
    require(digest_file(archive) == manifest["tileArchive"]["sha256"], "Navigation tile archive checksum mismatch")
    style_path = navigation / manifest["style"]["path"]
    require(digest_file(style_path) == manifest["style"]["sha256"], "Navigation style checksum mismatch")
    style = read_json(style_path)
    require(style["sources"]["regional"]["url"] == "mbtiles://navigation.mbtiles" and style["layers"], "Nonlocal or empty navigation style")
    with closing(sqlite3.connect(archive.as_uri() + "?mode=ro", uri=True)) as database:
        require(database.execute("PRAGMA integrity_check").fetchone()[0] == "ok", "Corrupt MBTiles")
        metadata = dict(database.execute("SELECT name,value FROM metadata"))
        require(metadata["format"] == "png", "Unexpected MBTiles format")
        count = 0
        for zoom, column, row, data in database.execute("SELECT * FROM tiles"):
            require(0 <= column < 2 ** zoom and 0 <= row < 2 ** zoom, "Invalid TMS address")
            with MemoryFile(data) as memory, memory.open() as image:
                require(image.width == image.height == 256 and image.driver == "PNG", "Invalid navigation tile")
                require(image.read(1).shape == (256, 256), "Undecodable navigation tile")
            count += 1
        require(count == manifest["tileArchive"]["tileCount"] and count > 0, "Missing navigation tiles")
    require(set(manifest["renderedFeatureCounts"]) == set(LAYERS), "Missing rendered navigation layer")
    return count
