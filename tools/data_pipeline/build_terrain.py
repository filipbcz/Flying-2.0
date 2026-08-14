#!/usr/bin/env python3
"""Build and validate the initial DMR 5G 50 x 50 km terrain slice package."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import sys
from io import StringIO
from pathlib import Path
from typing import Any, Iterable


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INPUT = REPO_ROOT / "Data" / "Terrain" / "Dmr5gSliceInput.json"
DEFAULT_OUTPUT = REPO_ROOT / "Data" / "Terrain" / "SliceManifest.json"
SCHEMA_VERSION = "flying.terrain-slice.v1"
INPUT_SCHEMA_VERSION = "flying.dmr5g-terrain-slice-input.v1"


def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_text(text: str) -> str:
    return sha256_bytes(text.encode("utf-8"))


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def stable_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def render_csv(rows: Iterable[dict[str, float | int | str]], fieldnames: list[str]) -> str:
    buffer = StringIO()
    writer = csv.DictWriter(buffer, fieldnames=fieldnames, lineterminator="\n")
    writer.writeheader()
    for row in rows:
        writer.writerow(row)
    return buffer.getvalue()


def source_root(input_path: Path, source: dict[str, Any]) -> Path:
    root = Path(source.get("sourceRoot", "."))
    return root if root.is_absolute() else input_path.parent / root


def load_source_tiles(input_path: Path, source: dict[str, Any]) -> list[dict[str, Any]]:
    root = source_root(input_path, source)
    transform = source["transform"]
    source_to_project = transform["sourceToProject"]
    height_transform = transform.get(
        "heightTransform", {"sourceHeightScale": 1.0, "orthometricOffsetM": 0.0}
    )
    loaded = []
    for tile in source["tiles"]:
        path = root / tile["path"]
        require(path.exists(), f"source tile file is missing: {path}")
        payload = path.read_bytes()
        declared = tile.get("checksum", {})
        if declared:
            require(declared.get("algorithm") == "sha256", f"{tile['id']} checksum algorithm must be sha256")
            require(declared.get("value") == sha256_bytes(payload), f"{tile['id']} source checksum mismatch")
        samples = []
        text = payload.decode("utf-8")
        for row in csv.DictReader(StringIO(text)):
            source_x_m = float(row["source_x_m"])
            source_y_m = float(row["source_y_m"])
            east_m = (
                source_x_m * float(source_to_project["eastFromSourceXScale"])
                + source_y_m * float(source_to_project.get("eastFromSourceYScale", 0.0))
                + float(source_to_project["eastOffsetM"])
            )
            north_m = (
                source_x_m * float(source_to_project.get("northFromSourceXScale", 0.0))
                + source_y_m * float(source_to_project["northFromSourceYScale"])
                + float(source_to_project["northOffsetM"])
            )
            orthometric = (
                float(row["bpv_orthometric_height_m"])
                * float(height_transform["sourceHeightScale"])
                + float(height_transform["orthometricOffsetM"])
            )
            samples.append(
                {
                    "sourceX": source_x_m,
                    "sourceY": source_y_m,
                    "eastM": round(east_m, 3),
                    "northM": round(north_m, 3),
                    "orthometricHeightM": round(orthometric, 3),
                    "waterMask": int(row.get("water_mask", "0")),
                    "materialMask": int(row.get("material_mask", "1")),
                }
            )
        require(samples, f"{tile['id']} has no DMR source samples")
        loaded.append({**tile, "samples": samples, "sourcePayload": text})
    return loaded


def tile_samples(tile: dict[str, Any]) -> list[dict[str, Any]]:
    return list(tile["samples"])


def sample_key(sample: dict[str, Any]) -> tuple[float, float]:
    return (float(sample["eastM"]), float(sample["northM"]))


def sample_lookup(tiles: list[dict[str, Any]]) -> dict[tuple[str, float, float], dict[str, Any]]:
    lookup: dict[tuple[str, float, float], dict[str, Any]] = {}
    for tile in tiles:
        for sample in tile_samples(tile):
            lookup[(tile["id"], float(sample["eastM"]), float(sample["northM"]))] = sample
    return lookup


def project_lookup(tiles: list[dict[str, Any]]) -> dict[tuple[float, float], dict[str, Any]]:
    lookup: dict[tuple[float, float], dict[str, Any]] = {}
    for tile in tiles:
        for sample in tile_samples(tile):
            lookup.setdefault(sample_key(sample), sample)
    return lookup


def normal_for(sample: dict[str, Any], samples: list[dict[str, Any]]) -> tuple[float, float, float]:
    coords = {sample_key(entry): float(entry["orthometricHeightM"]) for entry in samples}
    east = float(sample["eastM"])
    north = float(sample["northM"])
    spacing = min(
        sorted({abs(float(entry["eastM"]) - east) for entry in samples if float(entry["eastM"]) != east}) or [1.0]
    )
    east_minus = coords.get((east - spacing, north), float(sample["orthometricHeightM"]))
    east_plus = coords.get((east + spacing, north), float(sample["orthometricHeightM"]))
    north_minus = coords.get((east, north - spacing), float(sample["orthometricHeightM"]))
    north_plus = coords.get((east, north + spacing), float(sample["orthometricHeightM"]))
    dx = (east_plus - east_minus) / (2.0 * spacing)
    dy = (north_plus - north_minus) / (2.0 * spacing)
    length = math.sqrt(dx * dx + dy * dy + 1.0)
    return (-dx / length, -dy / length, 1.0 / length)


def selected_samples(samples: list[dict[str, Any]], stride: int) -> list[dict[str, Any]]:
    easts = sorted({float(sample["eastM"]) for sample in samples})
    norths = sorted({float(sample["northM"]) for sample in samples})
    selected_easts = set(easts[::stride])
    selected_norths = set(norths[::stride])
    return [
        sample
        for sample in samples
        if float(sample["eastM"]) in selected_easts and float(sample["northM"]) in selected_norths
    ]


def render_tile_csv(tile: dict[str, Any], stride: int, geoid_offset_m: float) -> str:
    samples = tile_samples(tile)
    rows = []
    for sample in selected_samples(samples, stride):
        normal = normal_for(sample, samples)
        orthometric = float(sample["orthometricHeightM"])
        is_water = int(sample.get("waterMask", 0)) != 0
        rows.append(
            {
                "east_m": f"{float(sample['eastM']):.3f}",
                "north_m": f"{float(sample['northM']):.3f}",
                "ellipsoidal_height_m": f"{orthometric + geoid_offset_m:.3f}",
                "orthometric_height_m": f"{orthometric:.3f}",
                "normal_east": f"{normal[0]:.9f}",
                "normal_north": f"{normal[1]:.9f}",
                "normal_up": f"{normal[2]:.9f}",
                "surface_type": "water" if is_water else "terrain",
                "physical_material": "water" if is_water else "grass",
                "water_mask": int(sample.get("waterMask", 0)),
                "material_mask": int(sample.get("materialMask", 1)),
            }
        )
    return render_csv(
        rows,
        [
            "east_m",
            "north_m",
            "ellipsoidal_height_m",
            "orthometric_height_m",
            "normal_east",
            "normal_north",
            "normal_up",
            "surface_type",
            "physical_material",
            "water_mask",
            "material_mask",
        ],
    )


def rows_cols(samples: list[dict[str, Any]], stride: int) -> tuple[int, int]:
    selected = selected_samples(samples, stride)
    return (
        len({float(sample["northM"]) for sample in selected}),
        len({float(sample["eastM"]) for sample in selected}),
    )


def tile_metadata(
    tile: dict[str, Any],
    path: str,
    payload: str,
    stride: int,
    sample_spacing_m: float,
) -> dict[str, Any]:
    rows, cols = rows_cols(tile_samples(tile), stride)
    return {
        "tileId": tile["id"],
        "path": path,
        "rows": rows,
        "cols": cols,
        "bounds": tile["bounds"],
        "sourceBounds": tile["sourceBounds"],
        "sourceSampleSpacingM": sample_spacing_m,
        "checksum": {"algorithm": "sha256", "value": sha256_text(payload)},
    }


def validate_input(source: dict[str, Any]) -> None:
    require(source.get("schemaVersion") == INPUT_SCHEMA_VERSION, "unexpected terrain slice input schema")
    coverage = source["coverage"]["bounds"]
    require(coverage["widthM"] == 50000 and coverage["heightM"] == 50000, "input coverage must be 50 x 50 km")
    require(source["coverage"].get("sampleSpacingM", 0) > 0, "coverage.sampleSpacingM must be positive")
    require(len(source.get("tiles", [])) >= 2, "at least two adjacent terrain tiles are required")
    require(source.get("controlPoints"), "supplied DMR control points are required")
    require(source.get("seamChecks"), "adjacent seam checks are required")
    for field in ("version", "effectiveDate"):
        require(source["sourceDataset"].get(field), f"sourceDataset.{field} is required")
    for tile in source["tiles"]:
        require("samples" not in tile, f"{tile['id']} must reference a DMR source tile file, not embed samples")
        require(tile.get("path"), f"{tile['id']} source path is required")
        require(tile.get("checksum", {}).get("value"), f"{tile['id']} source checksum is required")
        require(tile.get("sourceBounds"), f"{tile['id']} sourceBounds are required")


def validate_tile_coverage(source: dict[str, Any], tiles: list[dict[str, Any]]) -> None:
    coverage = source["coverage"]["bounds"]
    min_east = min(float(tile["bounds"]["minEastM"]) for tile in tiles)
    max_east = max(float(tile["bounds"]["maxEastM"]) for tile in tiles)
    min_north = min(float(tile["bounds"]["minNorthM"]) for tile in tiles)
    max_north = max(float(tile["bounds"]["maxNorthM"]) for tile in tiles)
    require(min_east <= coverage["minEastM"], "tile coverage does not reach western slice boundary")
    require(max_east >= coverage["minEastM"] + coverage["widthM"], "tile coverage does not reach eastern slice boundary")
    require(min_north <= coverage["minNorthM"], "tile coverage does not reach southern slice boundary")
    require(max_north >= coverage["minNorthM"] + coverage["heightM"], "tile coverage does not reach northern slice boundary")
    for tile in tiles:
        bounds = tile["bounds"]
        spacing = float(source["coverage"]["sampleSpacingM"])
        expected_cols = int(round((float(bounds["maxEastM"]) - float(bounds["minEastM"])) / spacing)) + 1
        expected_rows = int(round((float(bounds["maxNorthM"]) - float(bounds["minNorthM"])) / spacing)) + 1
        easts = sorted({float(sample["eastM"]) for sample in tile_samples(tile)})
        norths = sorted({float(sample["northM"]) for sample in tile_samples(tile)})
        require(len(easts) == expected_cols, f"{tile['id']} east sample count does not match bounds and spacing")
        require(len(norths) == expected_rows, f"{tile['id']} north sample count does not match bounds and spacing")
        require(easts[0] == float(bounds["minEastM"]), f"{tile['id']} grid does not start at minEastM")
        require(easts[-1] == float(bounds["maxEastM"]), f"{tile['id']} grid does not end at maxEastM")
        require(norths[0] == float(bounds["minNorthM"]), f"{tile['id']} grid does not start at minNorthM")
        require(norths[-1] == float(bounds["maxNorthM"]), f"{tile['id']} grid does not end at maxNorthM")
        for previous, current in zip(easts, easts[1:]):
            require(round(current - previous, 6) == spacing, f"{tile['id']} east grid spacing is not exact")
        for previous, current in zip(norths, norths[1:]):
            require(round(current - previous, 6) == spacing, f"{tile['id']} north grid spacing is not exact")
        require(len(tile_samples(tile)) == expected_cols * expected_rows, f"{tile['id']} sample grid is incomplete")
        for sample in tile_samples(tile):
            require(bounds["minEastM"] <= sample["eastM"] <= bounds["maxEastM"], f"{tile['id']} sample outside east bounds")
            require(bounds["minNorthM"] <= sample["northM"] <= bounds["maxNorthM"], f"{tile['id']} sample outside north bounds")
            source_bounds = tile["sourceBounds"]
            require(
                source_bounds["minSourceX"] <= sample["sourceX"] <= source_bounds["maxSourceX"],
                f"{tile['id']} sample outside source X bounds",
            )
            require(
                source_bounds["minSourceY"] <= sample["sourceY"] <= source_bounds["maxSourceY"],
                f"{tile['id']} sample outside source Y bounds",
            )


def validate_control_points(source: dict[str, Any], tiles: list[dict[str, Any]]) -> dict[str, Any]:
    lookup = project_lookup(tiles)
    tolerance = float(source["validationTolerances"]["dmrVerticalToleranceM"])
    results = []
    for point in source["controlPoints"]:
        key = (float(point["eastM"]), float(point["northM"]))
        require(key in lookup, f"control point {point['id']} is not covered by generated tile samples")
        actual = float(lookup[key]["orthometricHeightM"])
        expected = float(point["expectedOrthometricHeightM"])
        allowed = tolerance + float(point.get("sourceVerticalErrorM", 0.0))
        error = abs(actual - expected)
        results.append(
            {
                **point,
                "actualOrthometricHeightM": round(actual, 3),
                "absoluteErrorM": round(error, 3),
                "allowedErrorM": round(allowed, 3),
                "passed": error <= allowed,
            }
        )
    return {"points": results, "passed": all(point["passed"] for point in results)}


def validate_seams(source: dict[str, Any], tiles: list[dict[str, Any]]) -> dict[str, Any]:
    lookup = sample_lookup(tiles)
    max_step = 0.0
    compared = 0
    unmatched = 0
    failed = 0
    default_tolerance = float(source["validationTolerances"]["seamToleranceM"])
    for check in source["seamChecks"]:
        west_key = (check["westTileId"], float(check["eastM"]), float(check["northM"]))
        east_key = (check["eastTileId"], float(check["eastM"]), float(check["northM"]))
        if west_key not in lookup or east_key not in lookup:
            unmatched += 1
            continue
        compared += 1
        step = abs(float(lookup[west_key]["orthometricHeightM"]) - float(lookup[east_key]["orthometricHeightM"]))
        max_step = max(max_step, step)
        if step > float(check.get("toleranceM", default_tolerance)):
            failed += 1
    return {
        "adjacentPairCount": len({(check["westTileId"], check["eastTileId"]) for check in source["seamChecks"]}),
        "comparedSampleCount": compared,
        "unmatchedSampleCount": unmatched,
        "failedSampleCount": failed,
        "toleranceM": default_tolerance,
        "maxAbsStepM": round(max_step, 3),
        "passed": unmatched == 0 and failed == 0,
    }


def write_package_file(output_path: Path, relative_path: str, payload: str, write_files: bool) -> None:
    if not write_files:
        return
    target = output_path.parent / relative_path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(payload, encoding="utf-8")


def build_manifest(input_path: Path, source: dict[str, Any], output_path: Path, write_files: bool) -> dict[str, Any]:
    validate_input(source)
    tiles = load_source_tiles(input_path, source)
    validate_tile_coverage(source, tiles)

    geoid_offset_m = float(source["transform"]["geoid"]["undulationMeters"])
    sample_spacing_m = float(source["coverage"]["sampleSpacingM"])
    render_lods = []
    for lod in source["renderLods"]:
        rendered_tiles = []
        for tile in tiles:
            path = f"render/lod{lod['level']}/{tile['id']}.terrain.csv"
            payload = render_tile_csv(tile, int(lod["sampleStride"]), geoid_offset_m)
            write_package_file(output_path, path, payload, write_files)
            rendered_tiles.append(tile_metadata(tile, path, payload, int(lod["sampleStride"]), sample_spacing_m))
        render_lods.append({"level": lod["level"], "sampleStride": lod["sampleStride"], "tiles": rendered_tiles})

    collision_tiles = []
    for tile in tiles:
        path = f"collision/{tile['id']}.collision.csv"
        payload = render_tile_csv(tile, 1, geoid_offset_m)
        write_package_file(output_path, path, payload, write_files)
        collision_tiles.append(tile_metadata(tile, path, payload, 1, sample_spacing_m))

    control_points = validate_control_points(source, tiles)
    edge_continuity = validate_seams(source, tiles)
    boundary_adjustment = float(source["validationTolerances"]["maxBoundaryAdjustmentM"])

    return {
        "schemaVersion": SCHEMA_VERSION,
        "packageName": "flying-dmr5g-initial-terrain-slice",
        "packageVersion": source["sourceDataset"]["version"],
        "packageId": "dmr5g-cz-initial-50km-slice",
        "coverage": {
            "scope": "initial-terrain-slice",
            "countryCode": source["coverage"]["countryCode"],
            "bounds": source["coverage"]["bounds"],
            "offlineRuntime": True,
        },
        "provenance": {
            "sourceDataset": source["sourceDataset"]["name"],
            "publisher": source["sourceDataset"]["publisher"],
            "sourceVersion": source["sourceDataset"]["version"],
            "sourceEffectiveDate": source["sourceDataset"]["effectiveDate"],
            "sourceManifest": source["sourceDataset"]["sourceManifest"],
            "license": source["sourceDataset"]["license"],
            "licenseEvidence": "docs/licenses/source-attribution.yml",
            "inputPath": "Data/Terrain/Dmr5gSliceInput.json",
            "generatedBy": "tools/data_pipeline/build_terrain.py",
            "generatedAtUtc": "2026-08-14T00:00:00Z",
        },
        "sourceTiles": [
            {
                "id": tile["id"],
                "path": tile["path"],
                "format": "dmr5g-epsg5514-bpv-csv",
                "bounds": tile["bounds"],
                "sourceBounds": tile["sourceBounds"],
                "sourceSampleCount": len(tile_samples(tile)),
                "checksum": {"algorithm": "sha256", "value": sha256_text(tile["sourcePayload"])},
            }
            for tile in tiles
        ],
        "transforms": source["transform"],
        "transformValidation": {
            "sourceCrs": source["transform"]["sourceCrs"],
            "targetCrs": source["transform"]["targetCrs"],
            "sourceHeightSystem": source["transform"]["sourceHeightSystem"],
            "targetHeightSystem": source["transform"]["targetHeightSystem"],
            "operation": "affine-epsg5514-to-project-enu-plus-bpv-to-ellipsoidal-geoid-offset",
            "transformedSampleCount": sum(len(tile_samples(tile)) for tile in tiles),
            "sampleSpacingM": sample_spacing_m,
            "passed": True,
        },
        "terrainMetadata": {
            "heightEncoding": "meters-f64-csv",
            "heightFields": ["ellipsoidal_height_m", "orthometric_height_m"],
            "normalFrame": "project-local-ENU",
            "normalEncoding": "unit-vector-f64-csv",
            "waterMaskEncoding": "uint8-csv",
            "materialMaskEncoding": "uint8-csv",
            "edgeCleaned": False,
            "collisionTilesAreSeparate": True,
        },
        "renderLods": render_lods,
        "collisionTiles": collision_tiles,
        "validation": {
            "controlPoints": control_points,
            "edgeContinuity": edge_continuity,
            "boundaryCleaning": {
                "adjustedSampleCount": 0,
                "maxPreCleanStepM": edge_continuity["maxAbsStepM"],
                "maxAdjustmentM": boundary_adjustment,
            },
        },
    }


def validate_referenced_files(manifest: dict[str, Any], output_path: Path) -> list[str]:
    errors: list[str] = []
    for tile in manifest.get("sourceTiles", []):
        path = output_path.parent / tile["path"]
        if not path.exists():
            errors.append(f"source tile is missing: {tile['path']}")
            continue
        if sha256_bytes(path.read_bytes()) != tile["checksum"]["value"]:
            errors.append(f"source tile checksum mismatch: {tile['path']}")
    for collection in ("collisionTiles",):
        for tile in manifest.get(collection, []):
            path = output_path.parent / tile["path"]
            if not path.exists():
                errors.append(f"generated tile is missing: {tile['path']}")
                continue
            if sha256_bytes(path.read_bytes()) != tile["checksum"]["value"]:
                errors.append(f"generated tile checksum mismatch: {tile['path']}")
    for lod in manifest.get("renderLods", []):
        for tile in lod.get("tiles", []):
            path = output_path.parent / tile["path"]
            if not path.exists():
                errors.append(f"generated tile is missing: {tile['path']}")
                continue
            if sha256_bytes(path.read_bytes()) != tile["checksum"]["value"]:
                errors.append(f"generated tile checksum mismatch: {tile['path']}")
    return errors


def validate_manifest(manifest: dict[str, Any], output_path: Path) -> list[str]:
    errors: list[str] = []
    if manifest.get("schemaVersion") != SCHEMA_VERSION:
        errors.append("unexpected schemaVersion")
    coverage = manifest.get("coverage", {}).get("bounds", {})
    if coverage.get("widthM") != 50000 or coverage.get("heightM") != 50000:
        errors.append("terrain slice bounds are not 50 x 50 km")
    provenance = manifest.get("provenance", {})
    for field in ("sourceDataset", "publisher", "sourceVersion", "sourceEffectiveDate", "licenseEvidence"):
        if not provenance.get(field):
            errors.append(f"manifest provenance is missing {field}")
    if len(manifest.get("renderLods", [])) < 2:
        errors.append("manifest must include generated LOD levels")
    if not manifest.get("collisionTiles"):
        errors.append("manifest must include collision tiles")
    if not manifest.get("sourceTiles"):
        errors.append("manifest has no source tiles")
    transform_validation = manifest.get("transformValidation", {})
    if not transform_validation.get("passed"):
        errors.append("source-to-project transform validation did not pass")
    if transform_validation.get("transformedSampleCount", 0) <= 0:
        errors.append("manifest has no transformed DMR samples")
    tile_bounds = [tile.get("bounds", {}) for tile in manifest.get("sourceTiles", [])]
    if tile_bounds:
        if min(tile["minEastM"] for tile in tile_bounds) > coverage.get("minEastM", 0):
            errors.append("source tiles do not cover west boundary")
        if min(tile["minNorthM"] for tile in tile_bounds) > coverage.get("minNorthM", 0):
            errors.append("source tiles do not cover south boundary")
        if max(tile["maxEastM"] for tile in tile_bounds) < coverage.get("minEastM", 0) + coverage.get("widthM", 0):
            errors.append("source tiles do not cover east boundary")
        if max(tile["maxNorthM"] for tile in tile_bounds) < coverage.get("minNorthM", 0) + coverage.get("heightM", 0):
            errors.append("source tiles do not cover north boundary")
    for point in manifest.get("validation", {}).get("controlPoints", {}).get("points", []):
        if point.get("absoluteErrorM", 1e9) > point.get("allowedErrorM", 0.0):
            errors.append(f"control point {point.get('id')} exceeds DMR tolerance")
        if "source" not in point:
            errors.append(f"control point {point.get('id')} is missing source")
    edge = manifest.get("validation", {}).get("edgeContinuity", {})
    if edge.get("comparedSampleCount", 0) < 10:
        errors.append("seam validation must compare the full shared 50 km edge")
    if edge.get("unmatchedSampleCount") != 0 or edge.get("failedSampleCount") != 0:
        errors.append("adjacent tile seam continuity failed")
    if edge.get("maxAbsStepM", 1e9) > edge.get("toleranceM", 0.0):
        errors.append("adjacent tile seam step exceeds tolerance")
    for collection in ("sourceTiles", "collisionTiles"):
        for tile in manifest.get(collection, []):
            checksum = tile.get("checksum", {})
            if checksum.get("algorithm") != "sha256" or len(checksum.get("value", "")) != 64:
                errors.append(f"{collection} tile {tile.get('tileId', tile.get('id'))} has invalid checksum")
            if collection == "sourceTiles" and tile.get("format") != "dmr5g-epsg5514-bpv-csv":
                errors.append(f"source tile {tile.get('id')} is not raw DMR EPSG:5514/Bpv CSV")
            if "sourceSampleSpacingM" in tile and tile.get("sourceSampleSpacingM") != transform_validation.get("sampleSpacingM"):
                errors.append(f"{collection} tile {tile.get('tileId', tile.get('id'))} sample spacing drifted from input")
    for lod in manifest.get("renderLods", []):
        for tile in lod.get("tiles", []):
            checksum = tile.get("checksum", {})
            if checksum.get("algorithm") != "sha256" or len(checksum.get("value", "")) != 64:
                errors.append(f"LOD {lod.get('level')} tile {tile.get('tileId')} has invalid checksum")
    errors.extend(validate_referenced_files(manifest, output_path))
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the initial 50 x 50 km DMR 5G terrain slice manifest.")
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--validate", action="store_true", help="rebuild from source input and compare with existing output")
    args = parser.parse_args()

    try:
        source = json.loads(args.input.read_text(encoding="utf-8"))
        expected = build_manifest(args.input, source, args.output, write_files=not args.validate)
        if args.validate:
            actual = json.loads(args.output.read_text(encoding="utf-8"))
            if stable_json(actual) != stable_json(expected):
                raise ValueError("manifest does not match regenerated terrain package metadata")
            errors = validate_manifest(actual, args.output)
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(expected, indent=2) + "\n", encoding="utf-8")
            errors = validate_manifest(expected, args.output)
        if errors:
            for error in errors:
                print(error, file=sys.stderr)
            return 1
        return 0
    except Exception as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
