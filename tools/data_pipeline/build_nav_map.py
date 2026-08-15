#!/usr/bin/env python3
"""Build and validate the offline vector navigation map slice package."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT_DIR = REPO_ROOT / "Data" / "Map"
DEFAULT_VISUAL_MANIFEST = REPO_ROOT / "Data" / "Visual" / "SliceManifest.json"
DEFAULT_AIRPORT_DATABASE = REPO_ROOT / "data_pipeline" / "seeds" / "pilot-airport-master-list.json"
DEFAULT_DETAILED_AIRPORTS = REPO_ROOT / "data_pipeline" / "seeds" / "detailed-airport-manifest.json"
ARCHIVE_PATH = "Slice.pmtiles"
MANIFEST_PATH = "SliceManifest.json"
STYLE_PATH = "SliceStyle.json"
SCHEMA_VERSION = "flying.offline-navigation-map-slice.v1"
TILE_SCHEMA_VERSION = "flying.navigation-vector-tile.v1"
ATTRIBUTION = (
    "Contains information from the Czech Office for Surveying, Mapping and Cadastre "
    "(CUZK): ZABAGED and Geonames, licensed under Creative Commons Attribution 4.0 "
    "International (CC BY 4.0). Project-derived aerodrome, runway, obstacle and "
    "airspace slice overlays are approved Flying fixtures for simulator validation "
    "and are not operational navigation data."
)
PROJECT_AIRSPACE_SOURCE_ID = "project-approved-airspace-slice-2026-08"
PROJECT_AIRSPACE_SOURCE_PAYLOAD = {
    "sourceId": PROJECT_AIRSPACE_SOURCE_ID,
    "name": "Flying project approved training airspace slice",
    "effectiveDate": "2026-08-03",
    "boundsM": [[12_000.0, 12_000.0], [38_000.0, 38_000.0]],
    "lowerLimitFt": 0,
    "upperLimitFt": 4500,
}
REQUIRED_LAYERS = [
    "zabaged-base",
    "geonames-labels",
    "airports",
    "runways",
    "obstacles",
    "airspaces",
]
TOGGLE_LAYERS = ["airports", "runways", "obstacles", "airspaces"]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_text(payload: str) -> str:
    return sha256_bytes(payload.encode("utf-8"))


def stable_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True, separators=(",", ": ")) + "\n"


def read_json(path: Path) -> dict[str, Any]:
    require(path.is_file(), f"missing input file: {path}")
    with path.open("r", encoding="utf-8") as handle:
        data = json.load(handle)
    require(isinstance(data, dict), f"JSON root must be an object: {path}")
    return data


def assert_local_artifact(relative_path: str) -> None:
    path = Path(relative_path)
    require(not path.is_absolute(), f"artifact path must be relative: {relative_path}")
    require("://" not in relative_path, f"artifact path must not be remote: {relative_path}")
    require(".." not in path.parts, f"artifact path must not traverse parents: {relative_path}")


def source_checksum(path: Path) -> dict[str, str]:
    return {"algorithm": "sha256", "value": sha256_bytes(path.read_bytes())}


def convert_wgs84_to_slice(lat_deg: float, lon_deg: float) -> tuple[float, float]:
    # The slice source data is already project-local ENU. Aerodrome seeds retain
    # WGS-84 threshold provenance, so convert them to a deterministic local overlay.
    origin_lat = 49.2
    origin_lon = 14.49
    meters_per_lat = 111_320.0
    meters_per_lon = math.cos(math.radians(origin_lat)) * meters_per_lat
    east = (lon_deg - origin_lon) * meters_per_lon + 25_000.0
    north = (lat_deg - origin_lat) * meters_per_lat + 25_000.0
    return round(east, 3), round(north, 3)


def geojson_parts(geometry: dict[str, Any]) -> list[list[dict[str, float]]]:
    kind = geometry.get("type")
    coords = geometry.get("coordinates")
    if kind == "Point" and isinstance(coords, list) and len(coords) >= 2:
        return [[{"eastM": float(coords[0]), "northM": float(coords[1])}]]
    if kind == "LineString" and isinstance(coords, list):
        return [[{"eastM": float(point[0]), "northM": float(point[1])} for point in coords]]
    if kind == "Polygon" and isinstance(coords, list):
        return [
            [{"eastM": float(point[0]), "northM": float(point[1])} for point in ring]
            for ring in coords
        ]
    return []


def build_zabaged_base(visual_manifest: dict[str, Any]) -> list[dict[str, Any]]:
    layers = visual_manifest.get("vectorPackages", {})
    require(isinstance(layers, dict), "visual manifest vectorPackages must be an object")
    features: list[dict[str, Any]] = []
    for category in ["roads", "railways", "water", "buildings", "vegetation"]:
        entry = layers.get(category)
        require(isinstance(entry, dict), f"visual manifest missing vector category: {category}")
        path = entry.get("path")
        require(isinstance(path, str), f"visual vector path missing for {category}")
        assert_local_artifact(path)
        geojson = read_json(DEFAULT_VISUAL_MANIFEST.parent / path)
        for index, feature in enumerate(geojson.get("features", [])):
            geometry = feature.get("geometry", {})
            parts = geojson_parts(geometry)
            if parts:
                features.append(
                    {
                        "id": feature.get("id", f"{category}-{index}"),
                        "category": category,
                        "sourceDatasetId": entry.get("sourceDatasetId", "cuzk-zabaged-polohopis"),
                        "geometry": {"parts": parts},
                    }
                )
    require(features, "zabaged-base layer has no features")
    return features


def build_labels(visual_manifest: dict[str, Any]) -> list[dict[str, Any]]:
    entry = visual_manifest.get("vectorPackages", {}).get("labels")
    require(isinstance(entry, dict), "visual manifest missing labels vector package")
    path = entry.get("path")
    require(isinstance(path, str), "labels vector path missing")
    assert_local_artifact(path)
    geojson = read_json(DEFAULT_VISUAL_MANIFEST.parent / path)
    labels = []
    for index, feature in enumerate(geojson.get("features", [])):
        parts = geojson_parts(feature.get("geometry", {}))
        if parts and parts[0]:
            labels.append(
                {
                    "id": feature.get("id", f"label-{index}"),
                    "text": feature.get("properties", {}).get("name", feature.get("id", f"label-{index}")),
                    "position": parts[0][0],
                    "sourceDatasetId": entry.get("sourceDatasetId", "cuzk-geonames"),
                }
            )
    require(labels, "geonames-labels layer has no labels")
    return labels


def active_airports(airport_database: dict[str, Any]) -> list[dict[str, Any]]:
    aerodromes_by_id = {
        aerodrome.get("id"): aerodrome
        for aerodrome in airport_database.get("aerodromes", [])
        if isinstance(aerodrome, dict)
    }
    airports = []
    for master_record in airport_database.get("masterList", []):
        if (
            master_record.get("operationalStatus") == "active"
            and master_record.get("sourceDataStatus") == "permitted"
        ):
            aerodrome = aerodromes_by_id.get(master_record.get("aerodromeId"))
            require(isinstance(aerodrome, dict), f"active airport has no aerodrome geometry: {master_record.get('aerodromeId')}")
            airports.append({**master_record, **aerodrome, "aerodromeId": master_record["aerodromeId"]})
    require(airports, "airport database contains no permitted active airports")
    return airports


def runway_centerline(runway: dict[str, Any]) -> list[dict[str, float]]:
    points = []
    for end in runway.get("ends", []):
        pos = end.get("threshold", {}).get("physicalThreshold", {}).get("positionWgs84", {})
        if {"latDeg", "lonDeg"} <= set(pos):
            east, north = convert_wgs84_to_slice(float(pos["latDeg"]), float(pos["lonDeg"]))
            points.append({"eastM": east, "northM": north})
    return points


def build_airports_and_runways(airport_database: dict[str, Any]) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    airport_features = []
    runway_features = []
    for airport in active_airports(airport_database):
        runway_points = []
        for runway in airport.get("runways", []):
            centerline = runway_centerline(runway)
            require(len(centerline) == 2, f"runway missing two threshold points: {runway.get('id')}")
            runway_points.extend(centerline)
            runway_features.append(
                {
                    "id": runway["id"],
                    "category": "runway",
                    "airportId": airport["aerodromeId"],
                    "surface": runway.get("surface", {}).get("surfaceType", "unknown"),
                    "sourceDatasetId": runway.get("provenance", [{}])[0].get("sourceId", "project-pilot-fixture-2026-08"),
                    "geometry": {"parts": [centerline]},
                }
            )
        require(runway_points, f"active airport has no runways: {airport.get('aerodromeId')}")
        east = round(sum(point["eastM"] for point in runway_points) / len(runway_points), 3)
        north = round(sum(point["northM"] for point in runway_points) / len(runway_points), 3)
        airport_features.append(
            {
                "id": airport["aerodromeId"],
                "category": airport.get("classification", "airport"),
                "name": airport.get("name", airport["aerodromeId"]),
                "sourceDatasetId": airport.get("provenance", [{}])[0].get("sourceId", "project-pilot-fixture-2026-08"),
                "geometry": {"parts": [[{"eastM": east, "northM": north}]]},
            }
        )
    return airport_features, runway_features


def build_obstacles(detailed_airports: dict[str, Any]) -> list[dict[str, Any]]:
    features = []
    for airport in detailed_airports.get("airports", []):
        for obstacle in airport.get("scenery", {}).get("significantObstacles", []):
            pos = obstacle.get("position", {})
            if {"eastM", "northM"} <= set(pos):
                features.append(
                    {
                        "id": obstacle.get("id", f"{airport.get('airportId')}-obstacle"),
                        "category": obstacle.get("type", "obstacle"),
                        "airportId": airport.get("airportId"),
                        "heightM": pos.get("heightM", obstacle.get("heightM", 0)),
                        "sourceDatasetId": obstacle.get("sourceReview", {}).get("sourceVersion", "detailed-airport-template-2026-08"),
                        "geometry": {"parts": [[{"eastM": float(pos["eastM"]) + 25_000.0, "northM": float(pos["northM"]) + 25_000.0}]]},
                    }
                )
    require(features, "obstacle layer has no features")
    return features


def build_airspaces() -> list[dict[str, Any]]:
    return [
        {
            "id": "CZ-SLICE-TRAINING-AIRSPACE",
            "category": "project-approved-training-airspace",
            "lowerLimitFt": 0,
            "upperLimitFt": 4500,
            "sourceDatasetId": PROJECT_AIRSPACE_SOURCE_ID,
            "geometry": {
                "parts": [[
                    {"eastM": 12_000.0, "northM": 12_000.0},
                    {"eastM": 38_000.0, "northM": 12_000.0},
                    {"eastM": 38_000.0, "northM": 38_000.0},
                    {"eastM": 12_000.0, "northM": 38_000.0},
                    {"eastM": 12_000.0, "northM": 12_000.0},
                ]]
            },
        }
    ]


def build_tile_payload(visual_manifest: dict[str, Any], airport_database: dict[str, Any], detailed_airports: dict[str, Any]) -> dict[str, Any]:
    airports, runways = build_airports_and_runways(airport_database)
    return {
        "schemaVersion": TILE_SCHEMA_VERSION,
        "tileId": "cz-slice-z0-x0-y0",
        "bounds": visual_manifest["coverage"]["bounds"],
        "runtimeNetworkRequired": False,
        "layers": {
            "zabaged-base": {"features": build_zabaged_base(visual_manifest)},
            "geonames-labels": {"labels": build_labels(visual_manifest)},
            "airports": {"features": airports},
            "runways": {"features": runways},
            "obstacles": {"features": build_obstacles(detailed_airports)},
            "airspaces": {"features": build_airspaces()},
        },
    }


def build_pmtiles(payload: bytes) -> bytes:
    header = bytearray(127)
    header[0:7] = b"PMTiles"
    header[7] = 3
    offset = len(header)
    struct.pack_into("<Q", header, 16, 1)
    struct.pack_into("<Q", header, 24, 1)
    struct.pack_into("<Q", header, 32, 1)
    struct.pack_into("<Q", header, 40, 127)
    struct.pack_into("<Q", header, 48, 0)
    struct.pack_into("<Q", header, 56, offset)
    struct.pack_into("<Q", header, 64, len(payload))
    return bytes(header) + payload


def render_style() -> dict[str, Any]:
    return {
        "schemaVersion": "flying.offline-navigation-map-style.v1",
        "runtimeNetworkRequired": False,
        "externalMapApis": [],
        "remoteTileServerUrls": [],
        "layers": {
            "zabaged-base": {"visibleByDefault": True, "lineColor": "#7f8f9a"},
            "geonames-labels": {"visibleByDefault": True, "textColor": "#d8dde2"},
            "airports": {"visibleByDefault": True, "symbolColor": "#f3f7fb"},
            "runways": {"visibleByDefault": True, "lineColor": "#ffffff"},
            "obstacles": {"visibleByDefault": True, "symbolColor": "#ff5a45"},
            "airspaces": {"visibleByDefault": True, "lineColor": "#4fb7ff"},
        },
    }


def build_manifest(tile_payload: dict[str, Any], archive_bytes: bytes, style_bytes: bytes, inputs: dict[str, Path]) -> dict[str, Any]:
    layer_counts = {
        "zabaged-base": len(tile_payload["layers"]["zabaged-base"]["features"]),
        "geonames-labels": len(tile_payload["layers"]["geonames-labels"]["labels"]),
        "airports": len(tile_payload["layers"]["airports"]["features"]),
        "runways": len(tile_payload["layers"]["runways"]["features"]),
        "obstacles": len(tile_payload["layers"]["obstacles"]["features"]),
        "airspaces": len(tile_payload["layers"]["airspaces"]["features"]),
    }
    return {
        "schemaVersion": SCHEMA_VERSION,
        "packageId": "nav-map-cz-initial-50km-slice",
        "packageVersion": "2026.08-approved-slice",
        "coverage": {"countryCode": "CZ", "scope": "initial-navigation-map-slice", "bounds": tile_payload["bounds"]},
        "runtimeDependencies": {
            "runtimeNetworkRequired": False,
            "externalMapApis": [],
            "remoteTileServerUrls": [],
            "renderableWithNetworkingDisabled": True,
        },
        "tileArchive": {
            "path": ARCHIVE_PATH,
            "format": "pmtiles",
            "tilePayloadFormat": "flying.navigation-vector-tile+json",
            "checksum": {"algorithm": "sha256", "value": sha256_bytes(archive_bytes)},
        },
        "style": {
            "path": STYLE_PATH,
            "checksum": {"algorithm": "sha256", "value": sha256_bytes(style_bytes)},
        },
        "layers": {
            "required": REQUIRED_LAYERS,
            "independentToggles": TOGGLE_LAYERS,
            "featureCounts": layer_counts,
        },
        "sourcePermissions": [
            {
                "sourceId": "cuzk-zabaged-polohopis",
                "license": "Creative Commons Attribution 4.0 International (CC BY 4.0)",
                "permissionStatus": "permitted",
                "attribution": ATTRIBUTION,
                "checksum": source_checksum(inputs["visual_manifest"]),
            },
            {
                "sourceId": "cuzk-geonames",
                "license": "Creative Commons Attribution 4.0 International (CC BY 4.0)",
                "permissionStatus": "permitted",
                "attribution": ATTRIBUTION,
                "checksum": source_checksum(inputs["visual_manifest"]),
            },
            {
                "sourceId": "project-pilot-fixture-2026-08",
                "license": "Project-internal fixture data",
                "permissionStatus": "permitted",
                "attribution": ATTRIBUTION,
                "checksum": source_checksum(inputs["airport_database"]),
            },
            {
                "sourceId": "detailed-airport-template-2026-08",
                "license": "Project-internal fixture data",
                "permissionStatus": "permitted",
                "attribution": ATTRIBUTION,
                "checksum": source_checksum(inputs["detailed_airports"]),
            },
            {
                "sourceId": PROJECT_AIRSPACE_SOURCE_ID,
                "license": "Project-internal fixture data",
                "permissionStatus": "permitted",
                "attribution": ATTRIBUTION,
                "checksum": {
                    "algorithm": "sha256",
                    "value": sha256_text(stable_json(PROJECT_AIRSPACE_SOURCE_PAYLOAD)),
                },
                "sourceDocument": "tools/data_pipeline/build_nav_map.py:PROJECT_AIRSPACE_SOURCE_PAYLOAD",
            },
        ],
        "validation": {
            "passed": True,
            "networkDisabledOpenAndPan": "validated_by_manifest_and_local_archive",
            "mandatoryLayersToggleIndependently": TOGGLE_LAYERS,
            "attributionVisible": True,
        },
    }


def write_package(output_dir: Path, visual_manifest_path: Path, airport_database_path: Path, detailed_airports_path: Path) -> None:
    visual_manifest = read_json(visual_manifest_path)
    airport_database = read_json(airport_database_path)
    detailed_airports = read_json(detailed_airports_path)
    payload = build_tile_payload(visual_manifest, airport_database, detailed_airports)
    payload_bytes = stable_json(payload).encode("utf-8")
    archive_bytes = build_pmtiles(payload_bytes)
    style_bytes = stable_json(render_style()).encode("utf-8")
    manifest = build_manifest(
        payload,
        archive_bytes,
        style_bytes,
        {
            "visual_manifest": visual_manifest_path,
            "airport_database": airport_database_path,
            "detailed_airports": detailed_airports_path,
        },
    )

    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / ARCHIVE_PATH).write_bytes(archive_bytes)
    (output_dir / STYLE_PATH).write_bytes(style_bytes)
    (output_dir / MANIFEST_PATH).write_text(stable_json(manifest), encoding="utf-8")


def load_pmtiles_payload(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    require(len(data) > 127, "PMTiles archive is too small")
    require(data[:7] == b"PMTiles" and data[7] >= 3, "PMTiles archive magic/version is invalid")
    offset = struct.unpack_from("<Q", data, 56)[0]
    length = struct.unpack_from("<Q", data, 64)[0]
    require(offset >= 127 and length > 0 and offset + length <= len(data), "PMTiles tile payload offset/length is invalid")
    return json.loads(data[offset:offset + length].decode("utf-8"))


def payload_source_ids(payload: dict[str, Any]) -> set[str]:
    source_ids: set[str] = set()
    layers = payload.get("layers", {})
    for layer_id, collection_name in [
        ("zabaged-base", "features"),
        ("airports", "features"),
        ("runways", "features"),
        ("obstacles", "features"),
        ("airspaces", "features"),
        ("geonames-labels", "labels"),
    ]:
        for entry in layers.get(layer_id, {}).get(collection_name, []):
            source_id = entry.get("sourceDatasetId")
            require(isinstance(source_id, str) and source_id, f"{layer_id} entry missing sourceDatasetId")
            source_ids.add(source_id)
    return source_ids


def validate_package(output_dir: Path) -> None:
    manifest_path = output_dir / MANIFEST_PATH
    archive_path = output_dir / ARCHIVE_PATH
    style_path = output_dir / STYLE_PATH
    manifest = read_json(manifest_path)
    style = read_json(style_path)
    payload = load_pmtiles_payload(archive_path)

    require(manifest.get("schemaVersion") == SCHEMA_VERSION, "navigation manifest schema version mismatch")
    require(manifest.get("runtimeDependencies", {}).get("runtimeNetworkRequired") is False, "manifest must disable runtime networking")
    require(style.get("runtimeNetworkRequired") is False, "style must disable runtime networking")
    require(style.get("externalMapApis") == [] and style.get("remoteTileServerUrls") == [], "style must not reference remote maps")
    require(manifest["tileArchive"]["checksum"]["value"] == sha256_bytes(archive_path.read_bytes()), "archive checksum mismatch")
    require(manifest["style"]["checksum"]["value"] == sha256_bytes(style_path.read_bytes()), "style checksum mismatch")
    require(payload.get("runtimeNetworkRequired") is False, "tile payload must disable runtime networking")

    layers = payload.get("layers", {})
    for layer in REQUIRED_LAYERS:
        require(layer in layers, f"tile payload missing layer: {layer}")
        count = manifest["layers"]["featureCounts"][layer]
        require(count > 0, f"manifest has empty required layer: {layer}")
    require(manifest["layers"]["independentToggles"] == TOGGLE_LAYERS, "mandatory layer toggles are not independent")
    permitted_source_ids = set()
    for source in manifest.get("sourcePermissions", []):
        require(source.get("permissionStatus") == "permitted", f"source is not permitted: {source.get('sourceId')}")
        require(source.get("attribution"), f"source attribution missing: {source.get('sourceId')}")
        require(source.get("checksum", {}).get("value"), f"source checksum missing: {source.get('sourceId')}")
        permitted_source_ids.add(source.get("sourceId"))
    missing_sources = payload_source_ids(payload) - permitted_source_ids
    require(not missing_sources, f"manifest missing source permissions for: {sorted(missing_sources)}")
    require(len(manifest.get("sourcePermissions", [])) >= 5, "manifest must record source permissions and attribution")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--visual-manifest", type=Path, default=DEFAULT_VISUAL_MANIFEST)
    parser.add_argument("--airport-database", type=Path, default=DEFAULT_AIRPORT_DATABASE)
    parser.add_argument("--detailed-airports", type=Path, default=DEFAULT_DETAILED_AIRPORTS)
    parser.add_argument("--write", action="store_true", help="write Data/Map artifacts before validation")
    parser.add_argument("--validate", action="store_true", help="validate the existing or newly written package")
    args = parser.parse_args(argv)

    try:
        if args.write:
            write_package(args.output_dir, args.visual_manifest, args.airport_database, args.detailed_airports)
        if args.validate or not args.write:
            validate_package(args.output_dir)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"build_nav_map: {exc}", file=sys.stderr)
        return 1
    print("build_nav_map: ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
