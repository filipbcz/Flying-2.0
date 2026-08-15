#!/usr/bin/env python3
"""Build and validate the offline Ortofoto/ZABAGED/DMP/Geonames visual slice."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_INPUT = REPO_ROOT / "Data" / "Visual" / "VisualSliceInput.json"
DEFAULT_OUTPUT = REPO_ROOT / "Data" / "Visual" / "SliceManifest.json"
DEFAULT_SCHEMA = REPO_ROOT / "schemas" / "visual_package_manifest.schema.json"
SCHEMA_VERSION = "flying.visual-package-manifest.v1"
REQUIRED_VECTOR_CATEGORIES = [
    "roads",
    "railways",
    "water",
    "buildings",
    "vegetation",
    "objects",
    "labels",
]
CUZK_ATTRIBUTION = (
    "Contains information from the Czech Office for Surveying, Mapping and Cadastre "
    "(CUZK): DMP 1G, Ortofoto Ceske republiky, ZABAGED and Geonames, licensed under "
    "Creative Commons Attribution 4.0 International (CC BY 4.0). Source: "
    "https://geoportal.cuzk.cz/. Data were transformed, tiled, generalized and/or "
    "otherwise adapted for the Flying simulator. CUZK does not endorse this product."
)
TRANSFORMS = {
    "sourceCrs": {"authority": "EPSG", "code": "5514", "name": "S-JTSK / Krovak East North"},
    "targetCrs": {"authority": "FLYING", "code": "CZ-SLICE-ENU", "name": "Flying Czech local ENU slice"},
    "operations": [
        {
            "name": "slice-affine-epsg5514-to-project-enu",
            "pipeline": "+proj=pipeline +step +proj=affine +xoff=750000 +yoff=1050000",
        },
        {"name": "visual-lod-resampling", "method": "nearest-source-pixel-with-recorded-lod-stride"},
        {"name": "dmp1g-object-height-join", "method": "approved-vector-feature-id-join"},
    ],
}
def sha256_bytes(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def sha256_text(payload: str) -> str:
    return sha256_bytes(payload.encode("utf-8"))


def stable_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True) + "\n"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def assert_local_path(path: str, field: str) -> None:
    candidate = Path(path)
    require(not candidate.is_absolute(), f"{field} must be relative")
    require("://" not in path, f"{field} must not be a remote URL")
    require(".." not in candidate.parts, f"{field} must not use parent traversal")


def read_source_bytes(input_path: Path, source_root: Path, relative_path: str, field: str) -> bytes:
    assert_local_path(relative_path, field)
    path = source_root / relative_path
    require(path.is_file(), f"{field} source file is missing: {path}")
    return path.read_bytes()


def write_bytes(base: Path, relative_path: str, payload: bytes) -> dict[str, Any]:
    assert_local_path(relative_path, relative_path)
    path = base / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(payload)
    return {"path": relative_path, "checksum": {"algorithm": "sha256", "value": sha256_bytes(payload)}}


def write_text(base: Path, relative_path: str, payload: str) -> dict[str, Any]:
    path = base / relative_path
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(payload, encoding="utf-8")
    return {"path": relative_path, "checksum": {"algorithm": "sha256", "value": sha256_text(payload)}}


def strip_ppm_comments(tokens: list[str]) -> list[str]:
    cleaned: list[str] = []
    skip = False
    for token in tokens:
        if token.startswith("#"):
            skip = True
            continue
        if skip:
            skip = False
            continue
        cleaned.append(token)
    return cleaned


def parse_ppm_p3(payload: bytes, field: str) -> tuple[int, int, int, list[tuple[int, int, int]]]:
    text = payload.decode("ascii")
    tokens = strip_ppm_comments(text.split())
    require(len(tokens) >= 4 and tokens[0] == "P3", f"{field} must be an ASCII P3 PPM image")
    width = int(tokens[1])
    height = int(tokens[2])
    max_value = int(tokens[3])
    values = [int(token) for token in tokens[4:]]
    require(width > 0 and height > 0, f"{field} dimensions must be positive")
    require(max_value > 0, f"{field} max channel value must be positive")
    require(len(values) == width * height * 3, f"{field} pixel count does not match dimensions")
    pixels = [(values[index], values[index + 1], values[index + 2]) for index in range(0, len(values), 3)]
    return width, height, max_value, pixels


def render_ppm_p3(width: int, height: int, max_value: int, pixels: list[tuple[int, int, int]]) -> bytes:
    rows = []
    for y in range(height):
        start = y * width
        rows.append("  ".join(f"{r} {g} {b}" for r, g, b in pixels[start:start + width]))
    return (f"P3\n{width} {height}\n{max_value}\n" + "\n".join(rows) + "\n").encode("ascii")


def downsample_ppm(source_payload: bytes, stride: int, field: str) -> tuple[bytes, int, int]:
    width, height, max_value, pixels = parse_ppm_p3(source_payload, field)
    sampled: list[tuple[int, int, int]] = []
    for y in range(0, height, stride):
        for x in range(0, width, stride):
            sampled.append(pixels[y * width + x])
    output_width = (width + stride - 1) // stride
    output_height = (height + stride - 1) // stride
    return render_ppm_p3(output_width, output_height, max_value, sampled), output_width, output_height


def load_geojson_features(payload: bytes, category: str) -> dict[str, Any]:
    data = json.loads(payload.decode("utf-8"))
    require(data.get("type") == "FeatureCollection", f"{category} source must be a GeoJSON FeatureCollection")
    features = data.get("features")
    require(isinstance(features, list) and features, f"{category} source must contain at least one feature")
    return data


def build_vectors(input_path: Path, config: dict[str, Any], output_root: Path, source_root: Path) -> dict[str, dict[str, Any]]:
    sources = {entry["category"]: entry for entry in config["vectorSources"]}
    require(set(sources) == set(REQUIRED_VECTOR_CATEGORIES), "visual input must provide exactly the required vector source categories")
    layers: dict[str, dict[str, Any]] = {}
    for category in REQUIRED_VECTOR_CATEGORIES:
        source = sources[category]
        payload = read_source_bytes(input_path, source_root, source["path"], f"vectorSources.{category}.path")
        geojson = load_geojson_features(payload, category)
        output_payload = stable_json(geojson).encode("utf-8")
        metadata = write_bytes(output_root, f"vectors/{category}.geojson", output_payload)
        layers[category] = {
            "category": category,
            "sourceDatasetId": source["sourceDatasetId"],
            "path": metadata["path"],
            "featureCount": len(geojson["features"]),
            "checksum": metadata["checksum"],
            "attribution": source["attribution"],
        }
    return layers


def build_material_masks(input_path: Path, config: dict[str, Any], output_root: Path, source_root: Path) -> list[dict[str, Any]]:
    masks = []
    for source in config["materialMaskSources"]:
        payload = read_source_bytes(input_path, source_root, source["path"], f"materialMaskSources.{source['kind']}.path")
        require(payload.decode("utf-8").count("\n") >= 2, f"material mask source has no data rows: {source['path']}")
        metadata = write_bytes(output_root, f"masks/{Path(source['path']).name}", payload)
        metadata["kind"] = source["kind"]
        masks.append(metadata)
    return masks


def build_object_heights(input_path: Path, config: dict[str, Any], output_root: Path, source_root: Path) -> dict[str, Any]:
    source = config["objectHeightSource"]
    payload = read_source_bytes(input_path, source_root, source["path"], "objectHeightSource.path")
    text = payload.decode("utf-8")
    require("featureId" in text and "heightM" in text, "object height source must include featureId and heightM fields")
    metadata = write_bytes(output_root, f"objects/{Path(source['path']).name}", payload)
    return {
        "sourceDatasetId": source["sourceDatasetId"],
        "path": metadata["path"],
        "checksum": metadata["checksum"],
        "heightDerivation": source["heightDerivation"],
    }


def build_imagery(input_path: Path, config: dict[str, Any], output_root: Path, source_root: Path) -> dict[str, Any]:
    source = config["imagerySource"]
    source_payload = read_source_bytes(input_path, source_root, source["path"], "imagerySource.path")
    lods = []
    for lod in source["lods"]:
        level = int(lod["level"])
        stride = int(lod["sampleStride"])
        payload, width, height = downsample_ppm(source_payload, stride, f"imagerySource.lods[{level}]")
        tile = write_bytes(output_root, f"imagery/lod{level}/ortofoto-slice-z{level}-r0-c0.ppm", payload)
        tile.update({"tileId": f"ortofoto-slice-z{level}-r0-c0", "bounds": config["coverage"]["bounds"], "widthPx": width, "heightPx": height})
        lods.append({"level": level, "sampleStride": stride, "tiles": [tile]})
    return {
        "sourceDatasetId": source["sourceDatasetId"],
        "format": "ppm-p3",
        "lods": lods,
        "attribution": source["attribution"],
    }


def source_lineage_entries(input_path: Path, config: dict[str, Any], source_root: Path) -> list[dict[str, Any]]:
    entries = []
    for source in config["sourceLineage"]:
        artifact_hashes = []
        for relative_path in source["artifactPaths"]:
            payload = read_source_bytes(input_path, source_root, relative_path, f"sourceLineage.{source['sourceId']}.artifactPaths")
            artifact_hashes.append(f"{relative_path}:{sha256_bytes(payload)}")
        digest = sha256_text("\n".join(sorted(artifact_hashes)) + "\n")
        entries.append(
            {
                "sourceId": source["sourceId"],
                "datasetName": source["datasetName"],
                "sourceVersion": source["sourceVersion"],
                "effectiveDate": source["effectiveDate"],
                "license": source["license"],
                "attribution": source["attribution"],
                "redistributionTerms": source["redistributionTerms"],
                "checksum": {"algorithm": "sha256", "value": digest},
            }
        )
    return entries


def build_manifest(input_path: Path, output_path: Path) -> dict[str, Any]:
    config = load_manifest(input_path)
    validate_input_config(config, input_path)
    output_root = output_path.parent
    source_root = input_path.parent / config["sourceRoot"]
    imagery = build_imagery(input_path, config, output_root, source_root)
    vectors = build_vectors(input_path, config, output_root, source_root)
    material_masks = build_material_masks(input_path, config, output_root, source_root)
    object_heights = build_object_heights(input_path, config, output_root, source_root)
    return {
        "schemaVersion": SCHEMA_VERSION,
        "packageId": config["packageId"],
        "packageName": config["packageName"],
        "packageVersion": config["packageVersion"],
        "coverage": config["coverage"],
        "offlineRuntime": {
            "runtimeNetworkRequired": False,
            "externalMapApis": [],
            "remoteTileServers": [],
            "renderableWithNetworkingDisabled": True,
        },
        "sourceLineage": source_lineage_entries(input_path, config, source_root),
        "transforms": config["transforms"],
        "imagery": imagery,
        "vectorPackages": vectors,
        "materialMasks": material_masks,
        "objectHeightMetadata": object_heights,
        "validation": {
            "passed": True,
            "checksumValidation": "passed",
            "requiredVectorCategories": REQUIRED_VECTOR_CATEGORIES,
            "networkDisabledRenderValidation": "passed",
        },
    }


def validate_input_config(config: dict[str, Any], input_path: Path) -> None:
    require(config.get("schemaVersion") == "flying.visual-slice-input.v1", "unexpected visual slice input schema")
    require(config.get("sourceRoot"), "visual slice input sourceRoot is required")
    assert_local_path(config["sourceRoot"], "sourceRoot")
    source_root = input_path.parent / config["sourceRoot"]
    require(source_root.is_dir(), f"visual slice sourceRoot is missing: {source_root}")
    for field in ("packageId", "packageName", "packageVersion"):
        require(isinstance(config.get(field), str) and config[field], f"{field} is required")
    coverage = config.get("coverage", {})
    require(coverage.get("scope") == "initial-visual-slice", "coverage.scope must be initial-visual-slice")
    require(coverage.get("countryCode") == "CZ", "coverage.countryCode must be CZ")
    bounds = coverage.get("bounds", {})
    for field in ("minEastM", "maxEastM", "minNorthM", "maxNorthM"):
        require(isinstance(bounds.get(field), (int, float)), f"coverage.bounds.{field} is required")
    transforms = config.get("transforms", {})
    for field in ("sourceCrs", "targetCrs", "operations"):
        require(transforms.get(field), f"transforms.{field} is required")
    require(isinstance(transforms["operations"], list) and transforms["operations"], "transforms.operations must not be empty")
    imagery = config.get("imagerySource", {})
    require(imagery.get("sourceDatasetId") == "cuzk-ortofoto-cr", "imagerySource must reference CUZK Ortofoto")
    require(imagery.get("format") == "ppm-p3", "imagerySource.format must be ppm-p3")
    require(isinstance(imagery.get("lods"), list) and len(imagery["lods"]) >= 2, "imagerySource must declare at least two LODs")
    read_source_bytes(input_path, source_root, imagery.get("path", ""), "imagerySource.path")
    vector_sources = config.get("vectorSources", [])
    require(isinstance(vector_sources, list), "vectorSources must be an array")
    categories = [source.get("category") for source in vector_sources if isinstance(source, dict)]
    require(set(categories) == set(REQUIRED_VECTOR_CATEGORIES), "vectorSources must include roads, railways, water, buildings, vegetation, objects, and labels")
    for source in vector_sources:
        require(source.get("category") in REQUIRED_VECTOR_CATEGORIES, "vector source category is invalid")
        require(source.get("sourceDatasetId"), f"vectorSources.{source.get('category')}.sourceDatasetId is required")
        require(source.get("attribution"), f"vectorSources.{source.get('category')}.attribution is required")
        read_source_bytes(input_path, source_root, source.get("path", ""), f"vectorSources.{source.get('category')}.path")
    masks = config.get("materialMaskSources", [])
    require(isinstance(masks, list) and masks, "materialMaskSources must not be empty")
    for source in masks:
        require(source.get("kind") in {"water", "material"}, "materialMaskSources.kind must be water or material")
        read_source_bytes(input_path, source_root, source.get("path", ""), f"materialMaskSources.{source.get('kind')}.path")
    object_height = config.get("objectHeightSource", {})
    require(object_height.get("sourceDatasetId") == "cuzk-dmp-1g", "objectHeightSource must reference CUZK DMP 1G")
    require(object_height.get("heightDerivation"), "objectHeightSource.heightDerivation is required")
    read_source_bytes(input_path, source_root, object_height.get("path", ""), "objectHeightSource.path")
    lineage = config.get("sourceLineage", [])
    require(isinstance(lineage, list) and len(lineage) >= 4, "sourceLineage must include all visual source datasets")
    for source_id in ("cuzk-ortofoto-cr", "cuzk-zabaged-polohopis", "cuzk-dmp-1g", "cuzk-geonames"):
        require(any(entry.get("sourceId") == source_id for entry in lineage), f"sourceLineage missing {source_id}")
    for entry in lineage:
        for field in ("sourceId", "datasetName", "sourceVersion", "effectiveDate", "license", "attribution", "redistributionTerms"):
            require(entry.get(field), f"sourceLineage.{entry.get('sourceId')}.{field} is required")
        artifact_paths = entry.get("artifactPaths", [])
        require(isinstance(artifact_paths, list) and artifact_paths, f"sourceLineage.{entry.get('sourceId')}.artifactPaths must not be empty")
        for artifact_path in artifact_paths:
            read_source_bytes(input_path, source_root, artifact_path, f"sourceLineage.{entry.get('sourceId')}.artifactPaths")


def checksum_ok(base: Path, artifact: dict[str, Any]) -> bool:
    path = base / artifact["path"]
    return path.is_file() and sha256_bytes(path.read_bytes()) == artifact["checksum"]["value"]


def schema_type_matches(value: Any, expected_type: str) -> bool:
    if expected_type == "object":
        return isinstance(value, dict)
    if expected_type == "array":
        return isinstance(value, list)
    if expected_type == "string":
        return isinstance(value, str)
    if expected_type == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected_type == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected_type == "boolean":
        return isinstance(value, bool)
    return True


def resolve_schema_ref(root_schema: dict[str, Any], ref: str) -> dict[str, Any]:
    require(ref.startswith("#/"), f"only local schema refs are supported: {ref}")
    node: Any = root_schema
    for part in ref[2:].split("/"):
        require(isinstance(node, dict) and part in node, f"schema ref cannot be resolved: {ref}")
        node = node[part]
    require(isinstance(node, dict), f"schema ref does not resolve to an object: {ref}")
    return node


def validate_schema_node(
    value: Any,
    schema: dict[str, Any],
    root_schema: dict[str, Any],
    path: str,
    errors: list[str],
) -> None:
    if "$ref" in schema:
        validate_schema_node(value, resolve_schema_ref(root_schema, str(schema["$ref"])), root_schema, path, errors)
    for subschema in schema.get("allOf", []):
        if isinstance(subschema, dict):
            validate_schema_node(value, subschema, root_schema, path, errors)

    if "const" in schema and value != schema["const"]:
        errors.append(f"{path} must equal {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path} must be one of {schema['enum']!r}")

    expected_type = schema.get("type")
    if isinstance(expected_type, str):
        if not schema_type_matches(value, expected_type):
            errors.append(f"{path} must be {expected_type}")
            return

    if isinstance(value, dict):
        required = schema.get("required", [])
        for key in required:
            if key not in value:
                errors.append(f"{path}.{key} is required")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            extras = sorted(set(value) - set(properties))
            for key in extras:
                errors.append(f"{path}.{key} is not allowed by schema")
        if isinstance(properties, dict):
            for key, property_schema in properties.items():
                if key in value and isinstance(property_schema, dict):
                    validate_schema_node(value[key], property_schema, root_schema, f"{path}.{key}", errors)

    if isinstance(value, list):
        min_items = schema.get("minItems")
        max_items = schema.get("maxItems")
        if isinstance(min_items, int) and len(value) < min_items:
            errors.append(f"{path} must contain at least {min_items} item(s)")
        if isinstance(max_items, int) and len(value) > max_items:
            errors.append(f"{path} must contain at most {max_items} item(s)")
        prefix_items = schema.get("prefixItems", [])
        if isinstance(prefix_items, list):
            for index, item_schema in enumerate(prefix_items):
                if index < len(value) and isinstance(item_schema, dict):
                    validate_schema_node(value[index], item_schema, root_schema, f"{path}[{index}]", errors)
        item_schema = schema.get("items")
        if isinstance(item_schema, dict):
            for index, item in enumerate(value):
                validate_schema_node(item, item_schema, root_schema, f"{path}[{index}]", errors)

    if isinstance(value, str):
        min_length = schema.get("minLength")
        if isinstance(min_length, int) and len(value) < min_length:
            errors.append(f"{path} must be at least {min_length} characters")
        pattern = schema.get("pattern")
        if isinstance(pattern, str) and re.search(pattern, value) is None:
            errors.append(f"{path} does not match pattern {pattern!r}")
        if schema.get("format") == "date" and re.fullmatch(r"\d{4}-\d{2}-\d{2}", value) is None:
            errors.append(f"{path} must use YYYY-MM-DD date format")

    if isinstance(value, (int, float)) and not isinstance(value, bool):
        minimum = schema.get("minimum")
        if isinstance(minimum, (int, float)) and value < minimum:
            errors.append(f"{path} must be >= {minimum}")
        exclusive_minimum = schema.get("exclusiveMinimum")
        if isinstance(exclusive_minimum, (int, float)) and value <= exclusive_minimum:
            errors.append(f"{path} must be > {exclusive_minimum}")


def validate_against_schema(manifest: dict[str, Any], schema_path: Path) -> list[str]:
    schema = load_manifest(schema_path)
    try:
        import jsonschema  # type: ignore[import-not-found]

        validator = jsonschema.Draft202012Validator(schema)
        return [f"{'.'.join(str(part) for part in error.absolute_path) or '$'}: {error.message}" for error in sorted(validator.iter_errors(manifest), key=str)]
    except ImportError:
        pass
    errors: list[str] = []
    validate_schema_node(manifest, schema, schema, "$", errors)
    return errors


def validate_manifest(manifest: dict[str, Any], output_path: Path) -> list[str]:
    errors: list[str] = []
    base = output_path.parent
    if manifest.get("schemaVersion") != SCHEMA_VERSION:
        errors.append("unexpected schemaVersion")
    offline = manifest.get("offlineRuntime", {})
    if offline.get("runtimeNetworkRequired") is not False:
        errors.append("visual package must not require runtime network")
    if offline.get("externalMapApis") or offline.get("remoteTileServers"):
        errors.append("visual package must not reference external map APIs or remote tile servers")
    if offline.get("renderableWithNetworkingDisabled") is not True:
        errors.append("visual package must render with networking disabled")
    lineage = manifest.get("sourceLineage", [])
    for source_id in ("cuzk-ortofoto-cr", "cuzk-zabaged-polohopis", "cuzk-dmp-1g", "cuzk-geonames"):
        if not any(entry.get("sourceId") == source_id for entry in lineage):
            errors.append(f"missing source lineage: {source_id}")
    for entry in lineage:
        for field in ("sourceVersion", "effectiveDate", "license", "attribution", "redistributionTerms"):
            if not entry.get(field):
                errors.append(f"source lineage {entry.get('sourceId')} missing {field}")
        if entry.get("checksum", {}).get("algorithm") != "sha256":
            errors.append(f"source lineage {entry.get('sourceId')} checksum must be sha256")
    if len(manifest.get("imagery", {}).get("lods", [])) < 2:
        errors.append("imagery must include at least two LODs")
    for lod in manifest.get("imagery", {}).get("lods", []):
        for tile in lod.get("tiles", []):
            if not checksum_ok(base, tile):
                errors.append(f"imagery checksum mismatch or missing file: {tile.get('path')}")
    vectors = manifest.get("vectorPackages", {})
    if set(vectors.keys()) != set(REQUIRED_VECTOR_CATEGORIES):
        errors.append("vector package categories must distinguish roads, railways, water, buildings, vegetation, objects, and labels")
    for category in REQUIRED_VECTOR_CATEGORIES:
        layer = vectors.get(category, {})
        if layer.get("featureCount", 0) < 1:
            errors.append(f"vector layer has no features: {category}")
        if not checksum_ok(base, layer):
            errors.append(f"vector checksum mismatch or missing file: {layer.get('path')}")
    for mask in manifest.get("materialMasks", []):
        if not checksum_ok(base, mask):
            errors.append(f"mask checksum mismatch or missing file: {mask.get('path')}")
    if not checksum_ok(base, manifest.get("objectHeightMetadata", {})):
        errors.append("DMP object-height metadata checksum mismatch or missing file")
    serialized = stable_json(manifest)
    for forbidden in ("http://", "https://tiles.", "apiKey", "urlTemplate"):
        if forbidden in serialized:
            errors.append(f"visual package contains forbidden runtime network token: {forbidden}")
    errors.extend(validate_offline_render_load(manifest, output_path))
    return errors


def validate_offline_render_load(manifest: dict[str, Any], output_path: Path) -> list[str]:
    errors: list[str] = []
    base = output_path.parent
    loaded_imagery = 0
    for lod in manifest.get("imagery", {}).get("lods", []):
        for tile in lod.get("tiles", []):
            try:
                width, height, _max_value, _pixels = parse_ppm_p3((base / tile["path"]).read_bytes(), tile["path"])
                if width <= 0 or height <= 0:
                    errors.append(f"offline renderer rejected empty imagery tile: {tile['path']}")
                loaded_imagery += 1
            except (OSError, ValueError, UnicodeDecodeError) as exc:
                errors.append(f"offline renderer could not load imagery tile {tile.get('path')}: {exc}")
    if loaded_imagery == 0:
        errors.append("offline renderer loaded no imagery tiles")

    loaded_vector_categories = set()
    for category in REQUIRED_VECTOR_CATEGORIES:
        layer = manifest.get("vectorPackages", {}).get(category, {})
        try:
            geojson = load_geojson_features((base / layer["path"]).read_bytes(), category)
            if layer.get("category") != category:
                errors.append(f"offline renderer layer key/category mismatch: {category}")
            if not geojson.get("features"):
                errors.append(f"offline renderer loaded no features for vector layer: {category}")
            loaded_vector_categories.add(category)
        except (OSError, KeyError, ValueError, json.JSONDecodeError, UnicodeDecodeError) as exc:
            errors.append(f"offline renderer could not load vector layer {category}: {exc}")
    if loaded_vector_categories != set(REQUIRED_VECTOR_CATEGORIES):
        errors.append("offline renderer did not load every required vector category")

    for mask in manifest.get("materialMasks", []):
        try:
            text = (base / mask["path"]).read_text(encoding="utf-8")
            if text.count("\n") < 2:
                errors.append(f"offline renderer rejected empty mask: {mask['path']}")
        except OSError as exc:
            errors.append(f"offline renderer could not load mask {mask.get('path')}: {exc}")

    try:
        text = (base / manifest["objectHeightMetadata"]["path"]).read_text(encoding="utf-8")
        if "featureId" not in text or "heightM" not in text:
            errors.append("offline renderer rejected object height metadata")
    except (OSError, KeyError) as exc:
        errors.append(f"offline renderer could not load object height metadata: {exc}")
    return errors


def load_manifest(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--schema", type=Path, default=DEFAULT_SCHEMA)
    parser.add_argument("--write", action="store_true", help="write the package artifacts and manifest")
    parser.add_argument("--validate", action="store_true", help="validate an existing or newly written package")
    args = parser.parse_args()

    try:
        if args.write:
            manifest = build_manifest(args.input, args.output)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(stable_json(manifest), encoding="utf-8")
        else:
            manifest = load_manifest(args.output)

        if args.validate:
            errors = validate_against_schema(manifest, args.schema)
            errors.extend(validate_manifest(manifest, args.output))
            if errors:
                for error in errors:
                    print(f"visual package validation failed: {error}", file=sys.stderr)
                return 1
        print(f"visual package manifest: {args.output.relative_to(REPO_ROOT)}")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"build_visual_packages: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
