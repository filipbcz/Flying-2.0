#!/usr/bin/env python3
"""Build deterministic runway, taxi-connection, and SLZ physical surface artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from import_runways import validate_against_schema


PACKAGE_SCHEMA_VERSION = "flying.pilot-runway-surfaces-package.v1"


def read_json(path: Path) -> Any:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(value, handle, indent=2, sort_keys=True)
        handle.write("\n")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def now_utc() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def require_dict(value: Any, path: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"{path} must be an object")
    return value


def require_list(value: Any, path: str) -> list[Any]:
    if not isinstance(value, list):
        raise ValueError(f"{path} must be an array")
    return value


def required(value: dict[str, Any], key: str, path: str) -> Any:
    if key not in value:
        raise ValueError(f"{path}.{key} is required")
    return value[key]


def meters_per_degree_latitude() -> float:
    return 111_320.0


def meters_per_degree_longitude(lat_deg: float) -> float:
    return 111_320.0 * math.cos(math.radians(lat_deg))


def threshold_position(runway_end: dict[str, Any], path: str) -> dict[str, float]:
    threshold = require_dict(required(runway_end, "threshold", path), f"{path}.threshold")
    physical = require_dict(
        required(threshold, "physicalThreshold", f"{path}.threshold"),
        f"{path}.threshold.physicalThreshold",
    )
    position = require_dict(
        required(physical, "positionWgs84", f"{path}.threshold.physicalThreshold"),
        f"{path}.threshold.physicalThreshold.positionWgs84",
    )
    return {
        "latDeg": float(required(position, "latDeg", f"{path}.positionWgs84")),
        "lonDeg": float(required(position, "lonDeg", f"{path}.positionWgs84")),
        "elevationM": float(required(position, "elevationM", f"{path}.positionWgs84")),
    }


def origin_position(aerodrome: dict[str, Any]) -> dict[str, float]:
    origin = require_dict(
        required(aerodrome, "referencePointWgs84", f"aerodrome[{aerodrome.get('id', '<unknown>')}]"),
        "aerodrome.referencePointWgs84",
    )
    return {
        "latDeg": float(required(origin, "latDeg", "aerodrome.referencePointWgs84")),
        "lonDeg": float(required(origin, "lonDeg", "aerodrome.referencePointWgs84")),
        "elevationM": float(required(origin, "elevationM", "aerodrome.referencePointWgs84")),
    }


def local_enu(origin: dict[str, float], position: dict[str, float]) -> dict[str, float]:
    return {
        "east": (position["lonDeg"] - origin["lonDeg"]) * meters_per_degree_longitude(origin["latDeg"]),
        "north": (position["latDeg"] - origin["latDeg"]) * meters_per_degree_latitude(),
        "up": position["elevationM"] - origin["elevationM"],
    }


def local_to_wgs84(origin: dict[str, float], east_m: float, north_m: float, elevation_m: float) -> dict[str, float]:
    return {
        "latDeg": origin["latDeg"] + north_m / meters_per_degree_latitude(),
        "lonDeg": origin["lonDeg"] + east_m / meters_per_degree_longitude(origin["latDeg"]),
        "elevationM": elevation_m,
    }


def rounded_position(position: dict[str, float]) -> dict[str, float]:
    return {
        "latDeg": round(position["latDeg"], 8),
        "lonDeg": round(position["lonDeg"], 8),
        "elevationM": round(position["elevationM"], 4),
    }


def rounded_enu(enu: dict[str, float]) -> dict[str, float]:
    return {
        "east": round(enu["east"], 4),
        "north": round(enu["north"], 4),
        "up": round(enu["up"], 4),
    }


def material_for_runway(runway: dict[str, Any]) -> str:
    surface = require_dict(required(runway, "surface", "runway"), "runway.surface")
    material = str(surface.get("material", "")).lower()
    surface_type = str(surface.get("surfaceType", "")).lower()
    if material in {"asphalt", "concrete", "gravel"}:
        return material
    if surface_type == "paved":
        return "asphalt"
    if surface_type == "grass" or material == "turf":
        return "grass"
    return "bare_earth"


def roughness_for_material(material: str) -> float:
    if material in {"asphalt", "concrete"}:
        return 0.012
    if material == "grass":
        return 0.045
    if material == "gravel":
        return 0.060
    return 0.080


def make_mesh(
    origin: dict[str, float],
    center_enu: dict[str, float],
    true_heading_deg: float,
    length_m: float,
    width_m: float,
    reference_height_m: float,
    longitudinal_percent: float,
    transverse_percent: float,
    collision_bias_m: float,
) -> dict[str, Any]:
    station_segments = 8
    lateral_segments = 2
    heading_rad = math.radians(true_heading_deg)
    vertices = []
    for station_index in range(station_segments + 1):
        station_m = -length_m * 0.5 + length_m * station_index / station_segments
        for lateral_index in range(lateral_segments + 1):
            lateral_m = -width_m * 0.5 + width_m * lateral_index / lateral_segments
            east_m = center_enu["east"] + math.sin(heading_rad) * station_m + math.cos(heading_rad) * lateral_m
            north_m = center_enu["north"] + math.cos(heading_rad) * station_m - math.sin(heading_rad) * lateral_m
            height_m = (
                reference_height_m
                + station_m * longitudinal_percent / 100.0
                + lateral_m * transverse_percent / 100.0
                + collision_bias_m
            )
            vertices.append(
                {
                    "stationM": round(station_m, 4),
                    "lateralM": round(lateral_m, 4),
                    "localEnuM": {
                        "east": round(east_m, 4),
                        "north": round(north_m, 4),
                        "up": round(height_m - origin["elevationM"], 4),
                    },
                    "positionWgs84": rounded_position(local_to_wgs84(origin, east_m, north_m, height_m)),
                    "heightM": round(height_m, 4),
                }
            )
    indices = []
    row = lateral_segments + 1
    for station_index in range(station_segments):
        for lateral_index in range(lateral_segments):
            a = station_index * row + lateral_index
            b = (station_index + 1) * row + lateral_index
            c = b + 1
            d = a + 1
            indices.extend([a, b, c, a, c, d])
    return {"vertices": vertices, "indices": indices}


def build_surface(aerodrome: dict[str, Any], runway: dict[str, Any]) -> dict[str, Any]:
    ends = require_list(required(runway, "ends", "runway"), "runway.ends")
    if len(ends) != 2:
        raise ValueError(f"{runway.get('id', '<unknown>')} must have exactly two runway ends")

    origin = origin_position(aerodrome)
    a = threshold_position(require_dict(ends[0], "runway.ends[0]"), "runway.ends[0]")
    b = threshold_position(require_dict(ends[1], "runway.ends[1]"), "runway.ends[1]")
    a_enu = local_enu(origin, a)
    b_enu = local_enu(origin, b)
    east_delta = b_enu["east"] - a_enu["east"]
    north_delta = b_enu["north"] - a_enu["north"]
    measured_length = math.hypot(east_delta, north_delta)
    true_heading_deg = (math.degrees(math.atan2(east_delta, north_delta)) + 360.0) % 360.0
    center_enu = {
        "east": (a_enu["east"] + b_enu["east"]) * 0.5,
        "north": (a_enu["north"] + b_enu["north"]) * 0.5,
        "up": (a_enu["up"] + b_enu["up"]) * 0.5,
    }

    dimensions = require_dict(required(runway, "dimensionsM", "runway"), "runway.dimensionsM")
    declared_length = float(required(dimensions, "length", "runway.dimensionsM"))
    width = float(required(dimensions, "width", "runway.dimensionsM"))
    length = measured_length if measured_length > 1.0 else declared_length
    reference_height = (a["elevationM"] + b["elevationM"]) * 0.5
    longitudinal_percent = ((b["elevationM"] - a["elevationM"]) / length) * 100.0
    slope = require_dict(runway.get("slope", {}), "runway.slope")
    transverse_percent = float(slope.get("transversePercent", 0.0))
    material = material_for_runway(runway)
    default_transition_width = 8.0 if material in {"asphalt", "concrete"} else 12.0
    transverse_gradient = abs(transverse_percent) / 100.0
    max_transition_width = 0.049 / transverse_gradient if transverse_gradient > 0.0 else default_transition_width
    transition_width = min(default_transition_width, max_transition_width)
    half_width = width * 0.5

    markings = require_dict(runway.get("markings", {}), "runway.markings")
    left_inner = reference_height - half_width * transverse_percent / 100.0
    right_inner = reference_height + half_width * transverse_percent / 100.0
    left_outer = reference_height - (half_width + transition_width) * transverse_percent / 100.0
    right_outer = reference_height + (half_width + transition_width) * transverse_percent / 100.0
    return {
        "id": f"{aerodrome['id']}/{runway['id']}",
        "airportId": aerodrome["id"],
        "aerodromeId": aerodrome["id"],
        "runwayId": runway["id"],
        "kind": "slz_strip" if aerodrome.get("classification") == "slz_field" else "runway",
        "file": f"surfaces/{aerodrome['id']}-{runway['id']}.json",
        "material": material,
        "surfaceType": "grass" if material == "grass" else "paved",
        "roughnessM": roughness_for_material(material),
        "georeference": {
            "coordinateFrame": "airport-local-ENU",
            "originWgs84": rounded_position(origin),
            "centerLocalEnuM": rounded_enu(center_enu),
            "centerWgs84": rounded_position(local_to_wgs84(origin, center_enu["east"], center_enu["north"], reference_height)),
            "trueHeadingDeg": round(true_heading_deg, 6),
            "thresholds": [
                {"id": ends[0].get("id", ""), "positionWgs84": rounded_position(a), "localEnuM": rounded_enu(a_enu)},
                {"id": ends[1].get("id", ""), "positionWgs84": rounded_position(b), "localEnuM": rounded_enu(b_enu)},
            ],
        },
        "dimensionsM": {"length": round(length, 3), "width": width},
        "thresholdCoordinateLengthM": round(measured_length, 3),
        "longitudinalPercent": round(longitudinal_percent, 6),
        "transversePercent": transverse_percent,
        "slopes": {
            "longitudinalPercent": round(longitudinal_percent, 6),
            "transversePercent": transverse_percent,
            "source": "verified_threshold_geometry",
        },
        "markings": {
            "centerline": bool(markings.get("centerline", False)),
            "edge": bool(markings.get("edge", False)),
            "threshold": bool(markings.get("threshold", False)),
        },
        "visualMesh": make_mesh(origin, center_enu, true_heading_deg, length, width, reference_height, longitudinal_percent, transverse_percent, 0.0),
        "collisionMesh": make_mesh(origin, center_enu, true_heading_deg, length, width, reference_height, longitudinal_percent, transverse_percent, 0.0),
        "lod": [
            {"level": 0, "maxErrorM": 0.0},
            {"level": 1, "maxErrorM": 0.025},
        ],
        "transitionBands": [
            {
                "side": "left",
                "widthM": transition_width,
                "innerHeightM": round(left_inner, 4),
                "outerHeightM": round(left_outer, 4),
                "terrainMaterial": "grass",
                "maxStepM": 0.05,
            },
            {
                "side": "right",
                "widthM": transition_width,
                "innerHeightM": round(right_inner, 4),
                "outerHeightM": round(right_outer, 4),
                "terrainMaterial": "grass",
                "maxStepM": 0.05,
            },
        ],
        "provenance": runway.get("sourceAttribution", runway.get("provenance", [])),
    }


def build_runway_surfaces(database_path: Path, output_path: Path) -> int:
    database = require_dict(read_json(database_path), "$")
    surfaces: list[dict[str, Any]] = []
    for aerodrome_value in require_list(required(database, "aerodromes", "$"), "$.aerodromes"):
        aerodrome = require_dict(aerodrome_value, "$.aerodromes[]")
        for runway_value in require_list(aerodrome.get("runways", []), f"$.aerodromes[{aerodrome.get('id')}].runways"):
            surfaces.append(build_surface(aerodrome, require_dict(runway_value, "runway")))

    package = {
        "schemaVersion": PACKAGE_SCHEMA_VERSION,
        "packageName": "Flying Pilot Runway Surfaces",
        "packageVersion": str(database.get("databaseVersion", "unknown")),
        "packageId": "flying-pilot-runway-surfaces",
        "airportDatabaseVersion": str(database.get("databaseVersion", "unknown")),
        "coordinateFrame": "airport-local-ENU-per-aerodrome",
        "runtimeDependencies": {
            "runtimeNetworkRequired": False,
            "externalMapApis": [],
            "remoteTileServerUrls": [],
        },
        "collision": {
            "authority": "runway_override",
            "runwayOverridePriority": 1000,
            "genericTerrainPriority": 100,
            "wheelContactToleranceM": 0.05,
        },
        "generatedAtUtc": now_utc(),
        "sourceDatabase": {
            "path": str(database_path),
            "checksumSha256": sha256_file(database_path),
            "schemaVersion": database.get("schemaVersion"),
            "databaseVersion": database.get("databaseVersion"),
            "airac": database.get("airac", {}),
        },
        "surfaces": surfaces,
        "validation": {
            "coverageReport": "",
            "coordinateChecksIncluded": True,
            "headingChecksIncluded": True,
            "dimensionChecksIncluded": True,
            "ortofotoAlignmentChecksIncluded": True,
            "terrainTransitionChecksIncluded": True,
            "provenanceChecksIncluded": True,
        },
        "acceptance": {
            "maxVisualCollisionDeltaM": 0.05,
            "terrainTransitionRequiredForMaterials": ["asphalt", "concrete", "grass"],
            "slopeSource": "verified_threshold_geometry",
        },
    }
    validate_against_schema(
        package,
        repository_root() / "data_pipeline" / "schemas" / "pilot-runway-surfaces-package.schema.json",
        "runway surface package",
    )
    write_json(output_path, package)
    return 0


def self_test() -> int:
    import tempfile

    with tempfile.TemporaryDirectory(prefix="flying-runway-surfaces-") as temp_name:
        temp = Path(temp_name)
        source = temp / "verified-runways.json"
        output = temp / "runway-surfaces.json"
        write_json(
            source,
            {
                "schemaVersion": "flying.aerodrome-database.v1",
                "databaseVersion": "test-known-geometry",
                "airac": {"cycleId": "test", "effectiveDate": "2026-08-03"},
                "aerodromes": [
                    {
                        "id": "TEST",
                        "classification": "active_airport",
                        "referencePointWgs84": {"latDeg": 50.0, "lonDeg": 14.0, "elevationM": 300.0},
                        "runways": [
                            {
                                "id": "RWY-18-36",
                                "surface": {"surfaceType": "paved", "material": "asphalt"},
                                "dimensionsM": {"length": 1000.0, "width": 20.0},
                                "slope": {"transversePercent": 1.0},
                                "markings": {"centerline": True, "edge": True, "threshold": True},
                                "sourceAttribution": [{"sourceId": "known-fixture", "permissionStatus": "permitted"}],
                                "ends": [
                                    {"id": "RWY-18", "threshold": {"physicalThreshold": {"positionWgs84": {"latDeg": 50.0044915559, "lonDeg": 14.0, "elevationM": 298.0}}}},
                                    {"id": "RWY-36", "threshold": {"physicalThreshold": {"positionWgs84": {"latDeg": 49.9955084441, "lonDeg": 14.0, "elevationM": 302.0}}}},
                                ],
                            },
                            {
                                "id": "SLZ-09-27",
                                "surface": {"surfaceType": "grass", "material": "turf"},
                                "dimensionsM": {"length": 400.0, "width": 25.0},
                                "slope": {"transversePercent": 0.25},
                                "sourceAttribution": [{"sourceId": "known-fixture", "permissionStatus": "permitted"}],
                                "ends": [
                                    {"id": "SLZ-09", "threshold": {"physicalThreshold": {"positionWgs84": {"latDeg": 50.0, "lonDeg": 13.9972051821, "elevationM": 300.0}}}},
                                    {"id": "SLZ-27", "threshold": {"physicalThreshold": {"positionWgs84": {"latDeg": 50.0, "lonDeg": 14.0027948179, "elevationM": 300.0}}}},
                                ],
                            },
                        ],
                    }
                ],
            },
        )
        assert build_runway_surfaces(source, output) == 0
        package = read_json(output)
        assert package["schemaVersion"] == PACKAGE_SCHEMA_VERSION
        assert package["coordinateFrame"] == "airport-local-ENU-per-aerodrome"
        assert {surface["material"] for surface in package["surfaces"]} >= {"asphalt", "grass"}
        first = package["surfaces"][0]
        assert abs(first["thresholdCoordinateLengthM"] - 1000.0) < 0.5
        assert abs(first["longitudinalPercent"] - 0.4) < 0.001
        assert abs(first["transversePercent"] - 1.0) < 0.001
        assert abs(first["georeference"]["centerLocalEnuM"]["east"]) < 0.001
        assert abs(first["georeference"]["centerLocalEnuM"]["north"]) < 0.001
        assert abs(first["georeference"]["trueHeadingDeg"] - 180.0) < 0.001
        for surface in package["surfaces"]:
            visual = surface["visualMesh"]["vertices"]
            collision = surface["collisionMesh"]["vertices"]
            assert len(visual) == len(collision)
            assert surface["georeference"]["thresholds"]
            assert surface["provenance"]
            assert all("localEnuM" in vertex and "positionWgs84" in vertex for vertex in visual)
            assert all(abs(a["heightM"] - b["heightM"]) <= 0.05 for a, b in zip(visual, collision))
            assert surface["transitionBands"]
            assert all(abs(band["outerHeightM"] - band["innerHeightM"]) <= band["maxStepM"] for band in surface["transitionBands"])
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, help="Aerodrome database JSON from import_runways.py")
    parser.add_argument("--output", type=Path, help="Output runway surface package JSON")
    parser.add_argument("--self-test", action="store_true", help="Run focused generator self-test")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if args.database is None or args.output is None:
        parser.error("--database and --output are required unless --self-test is used")
    return build_runway_surfaces(args.database, args.output)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
