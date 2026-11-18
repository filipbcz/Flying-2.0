"""Offline production-format pilot processing; never promotes missing evidence to a pass."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
import warnings
from pathlib import Path

import numpy as np
import pyproj
import rasterio
from pyproj.enums import TransformDirection
from pyproj.transformer import TransformerGroup
from rasterio.enums import Resampling
from rasterio.errors import NotGeoreferencedWarning
from rasterio.features import rasterize
from rasterio.warp import transform_geom

from acquire_sources import (
    REPOSITORY, atomic_json, contained, digest_file, persistent_root,
    read_json, require, staging_root, storage_preflight,
)
from validate_sources import validate_source
from regional_acceptance import image_placement, stitched_mesh, survey_comparison, verify_mesh
from acquire_regional_evidence import (
    EVIDENCE_MANIFEST, acquisition_manifest, evidence_root, manifest_digest, verify as verify_evidence,
)
from acquire_sources import load_config


SCHEMA = "flying.regional-payload.v2"
GRIDS = ("cz_cuzk_table_-y-x_3_v1710.tif", "cz_cuzk_CR-2005.tif")
TILE_CELLS = 512
STRIDES = (1, 2, 4, 8, 16)


def atomic_copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(destination.name + ".tmp")
    shutil.copyfile(source, temporary)
    temporary.replace(destination)


def write_npz(path, **arrays):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("wb") as stream:
        np.savez_compressed(stream, **arrays)
    temporary.replace(path)


def write_raster(path, values, transform=None, crs=None, nodata=None, tags=None, mips=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    bands = values[np.newaxis] if values.ndim == 2 else values
    profile = dict(driver="GTiff", count=bands.shape[0], height=bands.shape[1],
                   width=bands.shape[2], dtype=bands.dtype, tiled=True,
                   blockxsize=256, blockysize=256, compress="deflate", nodata=nodata)
    if transform is not None:
        profile.update(transform=transform, crs=crs)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", NotGeoreferencedWarning)
        with rasterio.open(temporary, "w", **profile) as dataset:
            dataset.write(bands)
            dataset.update_tags(**(tags or {}))
            if mips:
                factors = [factor for factor in (2, 4, 8, 16, 32, 64)
                           if min(bands.shape[1:]) // factor >= 1]
                dataset.build_overviews(factors, Resampling.average)
                dataset.update_tags(ns="rio_overview", resampling="average")
    temporary.replace(path)


def normals_for(heights, affine):
    north_gradient, east_gradient = np.gradient(heights.astype("float64"))
    normal = np.stack((-east_gradient / affine.a, -north_gradient / affine.e,
                       np.ones_like(heights)))
    normal /= np.linalg.norm(normal, axis=0)
    return normal.astype("float32")


def lattice(start, end, stride):
    return np.unique(np.append(np.arange(start, end + 1, stride), end)).astype("int32")


def triangle_indices(rows, columns):
    base = (np.arange(rows - 1)[:, None] * columns + np.arange(columns - 1)).ravel()
    return np.stack((np.stack((base, base + columns, base + 1), axis=1),
                     np.stack((base + 1, base + columns, base + columns + 1), axis=1)),
                    axis=1).reshape(-1, 3).astype("uint32")


class Checkpoint:
    def __init__(self, root, identity, refresh_evidence=False, manifest=None):
        self.root = root
        self.path = root / "artifacts/regional-validation/checkpoint.json"
        self.identity = identity
        self.stages = {}
        self.reused = []
        self.executed = []
        if self.path.exists():
            previous = read_json(self.path)
            if previous["identity"] != identity and refresh_evidence:
                self.refresh(previous, identity, manifest)
                previous = read_json(self.path)
            require(previous["identity"] == identity,
                    "Checkpoint inputs/toolchain changed; use a new versioned output root")
            self.stages = previous["stages"]

    def refresh(self, previous, identity, manifest):
        prior = previous["identity"]
        mutable = {"sources", "processor", "acceptanceProcessor", "evidenceProcessor", "supplementalEvidence"}
        require({key: value for key, value in prior.items() if key not in mutable} ==
                {key: value for key, value in identity.items() if key not in mutable},
                "Evidence recovery cannot change geometry schema, tools, tiling or LODs")
        require(prior["processor"] in (identity["processor"],
                "7ab2383f25f9834cbf4e26fa2cd4ebc5fb756c2e87ea608b383c7faa0669a3d4",
                "09f9de4aca4d2ad08a60bdf5c13cb5d0ab723cdb9aecc819b666eb8d3aed1c4f",
                "96213e9744d1f96ca7d3ff3901422fe46763f9dee0d9ff530cd55ec2e2069f9b",
                "6bfa6b90c5dc8e73c7849e5619d208acd031f6c4fddd87ada98bfa2b97c48605") and
                prior["acceptanceProcessor"] in (identity["acceptanceProcessor"],
                "b3546084684c66b4b71b2a24d6fb0cb20e97b9ecc28b319b26971ada489d3c70",
                "55558d56fc1cb0ae1e01e67d810dc384e7950a563724ce587f32c60380df3922"),
                "Unreviewed processor change; geometry cannot be reused")
        lineage_path = self.root / "data/normalized/source-lineage.json"
        lineage = read_json(lineage_path)
        lineage_hash = digest_file(lineage_path)
        require(lineage_hash == previous["stages"]["source-lineage"][0]["sha256"] and
                prior["sources"] in (lineage_hash, manifest_digest(lineage)),
                "Previous source lineage checksum mismatch")
        require({key: value for key, value in lineage.items() if key != "evidence"} ==
                {key: value for key, value in manifest.items() if key != "evidence"},
                "Evidence recovery cannot change acquired payloads or coverage semantics")
        archive = self.root / "artifacts/regional-validation/checkpoint-before-evidence.json"
        if archive.exists():
            archive = archive.with_name(f"checkpoint-before-evidence-{prior['processor'][:12]}.json")
        require(not archive.exists(), "Recovery archive already exists; use the recovered checkpoint")
        placement_changed = any(prior.get("supplementalEvidence", {}).get(name) !=
                                identity.get("supplementalEvidence", {}).get(name)
                                for name in ("ortofoto/export-placement.json",))
        invalidated = [name for name in previous["stages"] if name in
                       ("source-lineage", "validation-reports", EVIDENCE_MANIFEST) or (name == "imagery-mips" and placement_changed) or
                       name.startswith(("ortofoto/", "validation/"))]
        retained = {name: entries for name, entries in previous["stages"].items() if name not in invalidated}
        for entries in retained.values():
            for entry in entries:
                require(digest_file(contained(self.root, entry["path"])) == entry["sha256"],
                        "Recovery payload checksum mismatch")
        atomic_json(archive, previous)
        atomic_json(self.path, dict(schemaVersion=SCHEMA, identity=identity, stages=retained))
        atomic_json(self.root / "artifacts/regional-validation/evidence-recovery-report.json",
                    dict(status="pass", priorIdentity=prior, currentIdentity=identity,
                         retainedStages=list(retained), invalidatedStages=invalidated,
                         geometryRegenerated=False, policy="Reviewed v2 evidence-only migration; rehash retained outputs"))

    def stage(self, name, sources, action):
        existing = self.stages.get(name, [])
        if existing and all(contained(self.root, entry["path"]).is_file() and
                            digest_file(contained(self.root, entry["path"])) == entry["sha256"]
                            for entry in existing):
            self.reused.append(name)
            return
        paths = action()
        require(paths, f"Empty production stage: {name}")
        self.stages[name] = [dict(path=path.relative_to(self.root).as_posix(),
                                  bytes=path.stat().st_size, sha256=digest_file(path),
                                  sourceIds=sources) for path in paths]
        atomic_json(self.path, dict(schemaVersion=SCHEMA, identity=self.identity, stages=self.stages))
        self.executed.append(name)


def verify_sources(source_root):
    manifest = acquisition_manifest(source_root)
    require(manifest["schemaVersion"] == "flying.private-source-acquisition.v1",
            "Expected the real, private acquired source collection")
    require(manifest["regionId"] == "ceska-trebova-pilot-10km", "Unexpected regional scope")
    findings = []
    for group, entries in manifest["sourceGroups"].items():
        for source in entries:
            contained(source_root, source["checksum"]["path"])
            validate_source(source, f"$.sourceGroups.{group}", source_root, True,
                            findings, require_rights=False)
    require(not findings, json.dumps([item.to_report_item() for item in findings]))
    for group in ("dmr", "dmp", "ortofoto", "zabaged", "geonames", "grids"):
        require(manifest["sourceGroups"].get(group), f"Missing required real source: {group}")
    if manifest.get("evidence"):
        verify_evidence(source_root, load_config())
    return manifest


def official_transform(grid_root):
    pyproj.network.set_network_enabled(False)
    pyproj.datadir.append_data_dir(str(grid_root.resolve()))
    group = TransformerGroup("EPSG:5514+8357", "EPSG:4937", always_xy=True,
                             allow_ballpark=False)
    candidates = [transformer for transformer in group.transformers
                  if all(grid in transformer.definition for grid in GRIDS)]
    require(group.best_available and candidates, "Both official CUZK grids are mandatory; no fallback")
    return min(candidates, key=lambda transformer: transformer.accuracy)


def normalize_height(source, destination, role):
    with rasterio.open(source) as dataset:
        require(dataset.count == 1 and dataset.dtypes[0] == "float32", "Expected float32 height TIFF")
        # CUZK ImageServer exports carry the correct authority, units and axes
        # in LOCAL_CS rather than a projected WKT. Accept only that exact known
        # representation; an arbitrary local or missing CRS is not EPSG:5514.
        export_crs = rasterio.crs.CRS.from_wkt(
            'LOCAL_CS["S-JTSK / Krovak East North",'
            'UNIT["metre",1,AUTHORITY["EPSG","9001"]],'
            'AXIS["Easting",EAST],AXIS["Northing",NORTH],'
            'AUTHORITY["EPSG","5514"]]')
        require(dataset.crs and (dataset.crs.to_authority() == ("EPSG", "5514") or
                dataset.crs.to_wkt() == export_crs.to_wkt()),
                "Source height authority must be EPSG:5514 or the known CUZK export WKT")
        require(dataset.transform.a > 0 and dataset.transform.e < 0 and
                dataset.transform.b == dataset.transform.d == 0, "Unsupported height raster axes")
        values = dataset.read(1, masked=True)
        require(values.count() == values.size and np.isfinite(values).all(),
                "Unfilled terrain coverage or invalid heights; do not interpolate away gaps")
        require(float(values.min()) < float(values.max()), "Degenerate height payload")
        write_raster(destination, np.asarray(values), dataset.transform, "EPSG:5514+8357",
                     dataset.nodata, dict(role=role, sourceWkt=dataset.crs.to_wkt(), verticalDatum="Baltic 1957 (EPSG:8357)",
                                          normalization="Authority WKT repair only; no height resampling"))
    return [destination]


def transformed_vectors(source, destination):
    collection = read_json(source)
    require(collection.get("type") == "FeatureCollection" and collection.get("features"),
            f"Empty or unsupported thematic source: {source}")
    for feature in collection["features"]:
        require(feature.get("geometry"), "Null vector geometry")
        feature["geometry"] = transform_geom("EPSG:4326", "EPSG:5514", feature["geometry"],
                                             precision=-1)
    collection["crs"] = {"type": "name", "properties": {"name": "EPSG:5514"}}
    atomic_json(destination, collection)
    return [destination]


def generate_tile(root, rows, columns, heights, normals, affine):
    paths = []
    tile = f"r{rows[0]:04d}-c{columns[0]:04d}"
    for level, stride in enumerate(STRIDES):
        row_indices = lattice(rows[0], rows[-1], stride)
        column_indices = lattice(columns[0], columns[-1], stride)
        east = affine.c + (column_indices + 0.5) * affine.a
        north = affine.f + (row_indices + 0.5) * affine.e
        sampled = heights[np.ix_(row_indices, column_indices)]
        sampled_normals = normals[:, row_indices[:, None], column_indices]
        path = root / f"data/generated/terrain/{tile}/lod{level}.npz"
        write_npz(path, east_m=east, north_m=north, baltic1957_height_m=sampled,
                  normal_enu=sampled_normals, source_rows=row_indices, source_columns=column_indices,
                  **stitched_mesh(row_indices, column_indices, heights, normals, affine))
        paths.append(path)
        if level == 0:
            east_grid, north_grid = np.meshgrid(east - east[0], north - north[0])
            vertices = np.stack((east_grid, north_grid, sampled), axis=-1).reshape(-1, 3).astype("float32")
            collision = root / f"data/generated/collision/{tile}.npz"
            write_npz(collision, origin_east_north_m=np.array((east[0], north[0])),
                      vertices_enu_m=vertices, triangles=triangle_indices(len(east_grid), len(east)),
                      shape=np.array(sampled.shape), source_rows=row_indices, source_columns=column_indices)
            paths.append(collision)
    return paths


def seam_report(root):
    tiles = {}
    for path in sorted((root / "data/generated/terrain").glob("*/lod*.npz")):
        with np.load(path, allow_pickle=False) as payload:
            verify_mesh(payload)
            edges = {side: (payload["mesh_vertices_east_north_baltic_m"][payload[f"edge_{side}"]],
                            payload["mesh_normal_enu"][payload[f"edge_{side}"]])
                     for side in ("north", "south", "west", "east")}
            tiles.setdefault(path.parent.name, []).append((path.stem, edges))
    pairs = []
    for levels in tiles.values():
        require({level for level, _ in levels} == {f"lod{index}" for index in range(len(STRIDES))},
                "Missing terrain LOD in seam validation")
    for name, levels in tiles.items():
        for other_name, other_levels in tiles.items():
            if name >= other_name:
                continue
            for side, other_side in (("east", "west"), ("south", "north")):
                first = levels[0][1][side][0]
                second = other_levels[0][1][other_side][0]
                if first.shape != second.shape or not np.array_equal(first[:, :2], second[:, :2]):
                    continue
                for level, edges in levels:
                    for other_level, other_edges in other_levels:
                        xyz, normals = edges[side]
                        other_xyz, other_normals = other_edges[other_side]
                        require(xyz.shape == other_xyz.shape, "Mixed-LOD boundary sample mismatch")
                        error = float(np.max(np.abs(xyz - other_xyz)))
                        normal_error = float(np.max(np.abs(normals - other_normals)))
                        require(error == normal_error == 0, "Mixed-LOD terrain seam discontinuity")
                        pairs.append(dict(first=name, second=other_name, firstLod=level,
                                          secondLod=other_level, coordinateAndHeightGapM=error,
                                          normalDelta=normal_error))
    require(pairs, "No adjacent terrain seams tested")
    return dict(status="pass", pairs=pairs, mixedLodStatus="pass",
                sameLodPairCount=sum(pair["firstLod"] == pair["secondLod"] for pair in pairs),
                mixedLodPairCount=sum(pair["firstLod"] != pair["secondLod"] for pair in pairs),
                topology="Full source-resolution perimeter with explicit boundary-cell triangle fans at every LOD",
                perimeter="Pixel-centre mesh domain; outer half-pixel border is not collision coverage")


def validate_payloads(root, require_acceptance=True):
    checkpoint = read_json(root / "artifacts/regional-validation/checkpoint.json")
    require(checkpoint["identity"]["schemaVersion"] == SCHEMA, "Outdated regional payload schema")
    require(checkpoint["identity"]["processor"] == digest_file(Path(__file__)) and
            checkpoint["identity"]["acceptanceProcessor"] == digest_file(Path(__file__).with_name("regional_acceptance.py")),
            "Payload was generated by a different processor")
    require(checkpoint["identity"].get("evidenceProcessor") ==
            digest_file(Path(__file__).with_name("acquire_regional_evidence.py")),
            "Payload was generated by a different evidence processor")
    count = 0
    for entries in checkpoint["stages"].values():
        for entry in entries:
            path = contained(root, entry["path"])
            require(path.is_file() and digest_file(path) == entry["sha256"], f"Payload checksum mismatch: {path}")
            count += 1
    for path in (root / "data/generated/collision").glob("*.npz"):
        with np.load(path, allow_pickle=False) as collision, np.load(
                root / f"data/generated/terrain/{path.stem}/lod0.npz", allow_pickle=False) as terrain:
            heights = terrain["baltic1957_height_m"]
            require(np.array_equal(collision["vertices_enu_m"][:, 2], heights.ravel()),
                    "Collision must contain DMR heights only")
            triangles = collision["triangles"]
            require(np.array_equal(triangles, triangle_indices(*heights.shape)), "Invalid collision topology")
    seams = seam_report(root)
    source_root = root / "data/sources"
    manifest = {}
    if (source_root / "acquisition-manifest.json").is_file():
        manifest = verify_sources(source_root)
        require(manifest_digest(manifest) == checkpoint["identity"]["sources"],
                "Checkpoint source identity mismatch")
        require(read_json(root / "data/normalized/source-lineage.json") == manifest,
                "Normalized source lineage differs from acquisition")
        for entry in manifest.get("evidence", []):
            require(digest_file(contained(root / "data/normalized", entry["path"])) == entry["sha256"],
                    "Normalized evidence checksum mismatch")
    if require_acceptance:
        require(source_root.is_dir() and manifest.get("evidence"), "Managed evidence source collection is required")
        coverage = read_json(root / "artifacts/regional-validation/coverage-report.json")
        require(coverage["status"] == "pass" and not coverage["blockers"],
                "Regional acceptance blocked: " + "; ".join(coverage["blockers"]))
        survey = survey_comparison(root)
        require(survey["status"] == "pass", survey["reason"] or "Independent vertical plausibility validation failed")
        placement = read_json(root / "artifacts/regional-validation/imagery-report.json")
        require(placement["status"] == "pass", "Imagery has no verified placement")
        with rasterio.open(root / "data/generated/imagery/ortofoto-mips.tif") as image:
            require(image.crs and image.crs.to_string() == placement["crs"] and
                    list(image.transform) == placement["transform"] and image.overviews(1),
                    "Imagery placement or mip levels differ from evidence")
    return dict(status="pass", validationKind="acceptance" if require_acceptance else "integrity-only",
                filesRehashed=count, collision="DMR height and topology verified",
                seamPairCount=len(seams["pairs"]))


def build(source_root, root, refresh_evidence=False):
    free_bytes = storage_preflight(root)
    manifest = verify_sources(source_root)
    evidence_source = evidence_root(source_root)
    identity = dict(schemaVersion=SCHEMA, sources=manifest_digest(manifest),
                    processor=digest_file(Path(__file__)),
                    acceptanceProcessor=digest_file(Path(__file__).with_name("regional_acceptance.py")),
                    evidenceProcessor=digest_file(Path(__file__).with_name("acquire_regional_evidence.py")),
                    supplementalEvidence={name: digest_file(evidence_source / name) for name in
                        ("ortofoto/export-placement.json", "validation/height-controls.json", EVIDENCE_MANIFEST)
                        if (evidence_source / name).is_file()}, proj=pyproj.proj_version_str,
                    gdal=rasterio.__gdal_version__, numpy=np.__version__, tileCells=TILE_CELLS,
                    strides=STRIDES)
    identity = json.loads(json.dumps(identity))
    checkpoint = Checkpoint(root, identity, refresh_evidence=refresh_evidence, manifest=manifest)
    groups = manifest["sourceGroups"]
    source_paths = {entry["id"]: contained(source_root, entry["checksum"]["path"])
                    for entries in groups.values() for entry in entries}
    normalized = root / "data/normalized"

    def lineage():
        path = normalized / "source-lineage.json"
        atomic_json(path, manifest)
        return [path]

    checkpoint.stage("source-lineage", list(source_paths), lineage)
    for grid in groups["grids"]:
        source = source_paths[grid["id"]]
        destination = normalized / "grids" / source.name

        def copy_grid(source=source, destination=destination):
            atomic_copy(source, destination)
            return [destination]

        checkpoint.stage(grid["id"], [grid["id"]], copy_grid)
    evidence_paths = set(("ortofoto/export-placement.json", "validation/height-controls.json"))
    if (evidence_source / EVIDENCE_MANIFEST).is_file():
        evidence_paths.add(EVIDENCE_MANIFEST)
    for entry in manifest.get("evidence", []):
        evidence_paths.add(entry["path"])
        if (evidence_source / (entry["path"] + ".provenance.json")).is_file():
            evidence_paths.add(entry["path"] + ".provenance.json")
    for name in sorted(evidence_paths):
        if (evidence_source / name).is_file():
            def copy_evidence(name=name):
                destination = normalized / name
                atomic_copy(evidence_source / name, destination)
                return [destination]
            checkpoint.stage(name, list(source_paths), copy_evidence)
    transformer = official_transform(normalized / "grids")
    for group, role in (("dmr", "bare-earth"), ("dmp", "surface-objects-only; never bare-earth or collision")):
        require(len(groups[group]) == 1, f"Expected one regional {group} export")
        source = groups[group][0]
        checkpoint.stage(f"normalize-{group}", [source["id"]],
                         lambda source=source, group=group, role=role: normalize_height(
                             source_paths[source["id"]], normalized / f"{group}.tif", role))
    with rasterio.open(normalized / "dmr.tif") as dataset:
        heights = dataset.read(1)
        affine = dataset.transform
        raster_bounds = list(dataset.bounds)
    with rasterio.open(normalized / "dmp.tif") as dataset:
        require(dataset.transform == affine and dataset.shape == heights.shape, "DMR/DMP grid mismatch")
        surfaces = dataset.read(1)
    normals = normals_for(heights, affine)
    for row_start in range(0, heights.shape[0] - 1, TILE_CELLS):
        for column_start in range(0, heights.shape[1] - 1, TILE_CELLS):
            rows = (row_start, min(row_start + TILE_CELLS, heights.shape[0] - 1))
            columns = (column_start, min(column_start + TILE_CELLS, heights.shape[1] - 1))
            checkpoint.stage(f"terrain-{row_start}-{column_start}", [groups["dmr"][0]["id"]],
                             lambda rows=rows, columns=columns: generate_tile(
                                 root, rows, columns, heights, normals, affine))

    def object_metadata():
        path = root / "data/generated/objects/dmp-minus-dmr.tif"
        write_raster(path, surfaces - heights, affine, "EPSG:5514", tags=dict(
            role="surface-object-height-metadata-only", unit="metre",
            negativeValues="Preserved; surveys are not assumed simultaneous; not building heights"))
        return [path]

    checkpoint.stage("surface-object-metadata", [groups["dmr"][0]["id"], groups["dmp"][0]["id"]], object_metadata)
    vector_ids = []
    for source in groups["zabaged"] + groups["geonames"]:
        destination = normalized / "vectors" / (source["id"] + ".geojson")
        checkpoint.stage(source["id"], [source["id"]],
                         lambda source=source, destination=destination: transformed_vectors(
                             source_paths[source["id"]], destination))
        vector_ids.append(source["id"])

    def masks():
        paths = []
        for theme in ("water", "roads", "forest"):
            source = normalized / f"vectors/zabaged-{theme}.geojson"
            features = read_json(source)["features"]
            shapes = [(feature["geometry"], 1) for feature in features]
            mask = rasterize(shapes, out_shape=heights.shape, transform=affine,
                             fill=0, all_touched=True, dtype="uint8")
            path = root / f"data/generated/masks/{theme}.tif"
            write_raster(path, mask, affine, "EPSG:5514", tags=dict(
                role=f"{theme}-presence", rasterization="all_touched; no inferred road width"))
            paths.append(path)
        return paths

    checkpoint.stage("thematic-masks", vector_ids, masks)

    def imagery():
        require(len(groups["ortofoto"]) == 1, "Expected one orthophoto export")
        source = groups["ortofoto"][0]
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", NotGeoreferencedWarning)
            with rasterio.open(source_paths[source["id"]]) as dataset:
                require(dataset.count == 3 and dataset.dtypes[0] == "uint8", "Expected real RGB JPEG")
                pixels = dataset.read()
        original = normalized / "imagery/ortofoto.jpg"
        atomic_copy(source_paths[source["id"]], original)
        paths = [original]
        report_path = root / "artifacts/regional-validation/imagery-report.json"
        try:
            transform, crs, evidence = image_placement(evidence_source, source, pixels.shape[2], pixels.shape[1])
        except (ValueError, OSError, KeyError, TypeError) as error:
            report = dict(status="blocked", reason=str(error), imageSha256=source["checksum"]["value"])
        else:
            path = root / "data/generated/imagery/ortofoto-mips.tif"
            write_raster(path, pixels, transform, crs, tags=dict(
                role="regional-orthophoto", placement="returned-export-response",
                sourceSha256=source["checksum"]["value"], placementSha256=digest_file(evidence)), mips=True)
            paths.append(path)
            report = dict(status="pass", crs=crs.to_string(), transform=list(transform),
                          evidenceSha256=digest_file(evidence), imageSha256=source["checksum"]["value"])
        atomic_json(report_path, report)
        return paths + [report_path]

    checkpoint.stage("imagery-mips", [groups["ortofoto"][0]["id"]], imagery)

    def reports():
        directory = root / "artifacts/regional-validation"
        selected_rows = np.linspace(0, heights.shape[0] - 1, 17).astype("int32")
        selected_columns = np.linspace(0, heights.shape[1] - 1, 17).astype("int32")
        row_grid, column_grid = np.meshgrid(selected_rows, selected_columns, indexing="ij")
        east = affine.c + (column_grid.ravel() + .5) * affine.a
        north = affine.f + (row_grid.ravel() + .5) * affine.e
        source_height = heights[row_grid, column_grid].ravel().astype("float64")
        longitude, latitude, ellipsoidal = transformer.transform(east, north, source_height, errcheck=True)
        back_east, back_north, back_height = transformer.transform(longitude, latitude, ellipsoidal,
                direction=TransformDirection.INVERSE, errcheck=True)
        roundtrip = float(np.max(np.abs(back_height - source_height)))
        require(roundtrip <= 0.10, "Official-grid height round-trip exceeds 0.10 m")
        with rasterio.open(source_paths[groups["dmr"][0]["id"]]) as dataset:
            original = dataset.read(1)
        introduced = float(np.max(np.abs(original.astype("float64") - heights)))
        require(introduced <= .10, "Height normalization adds more than 0.10 m")
        controls = [dict(eastM=float(east[index]), northM=float(north[index]),
                         baltic1957M=float(source_height[index]), longitudeDeg=float(longitude[index]),
                         latitudeDeg=float(latitude[index]), etrs89EllipsoidalM=float(ellipsoidal[index]))
                    for index in range(len(east))]
        try:
            survey = survey_comparison(root)
        except (ValueError, OSError, KeyError, TypeError) as error:
            survey = dict(status="blocked", reason=str(error), independentSurveyControlsAvailable=False)
        transformations = dict(status="pass" if survey["status"] == "pass" else "blocked",
            sourceCrs="EPSG:5514+8357", targetCrs="EPSG:4937", axisOrder="always_xy; longitude,latitude,height",
            proj=pyproj.proj_version_str, gdal=rasterio.__gdal_version__, networkEnabled=False,
            pipeline=transformer.definition, operation=transformer.description,
            operationDeclaredAccuracyM=transformer.accuracy, officialGrids=list(GRIDS),
            gridChecksums={grid: digest_file(normalized / "grids" / grid) for grid in GRIDS},
            terrainStorage="Unresampled Baltic 1957; ellipsoidal heights exist only in transformation controls",
            horizontalRoundtripMaxM=float(max(np.max(np.abs(back_east - east)), np.max(np.abs(back_north - north)))),
            heightRoundtripMaxM=roundtrip, normalizationMaxAddedHeightErrorM=introduced,
            controlKind="289 source-sample numerical controls, NOT independently surveyed control points",
            surveyComparison=survey, numericalSamples=controls,
            vectorOperation="GDAL transform_geom EPSG:4326 to EPSG:5514; service WGS84 ensemble, no epoch claim",
            vectorProjPipeline=pyproj.Transformer.from_crs(4326, 5514, always_xy=True).definition)
        seams = seam_report(root)
        placement = read_json(directory / "imagery-report.json")
        blockers = []
        if placement["status"] != "pass":
            blockers.append(placement["reason"])
        if survey["status"] != "pass":
            blockers.append(survey.get("reason", "Comparable ground control exceeds declared dataset threshold"))
        coverage = dict(status="blocked" if blockers else "pass", regionId=manifest["regionId"], requestedBounds=manifest["bounds"],
            terrainRasterBoundsEpsg5514=raster_bounds, width=heights.shape[1], height=heights.shape[0],
            validTerrainCells=int(np.isfinite(heights).sum()), missingTerrainCells=0,
            resolutionM=[affine.a, -affine.e], terrainTiles=len(list((root / "data/generated/terrain").iterdir())),
            terrainSource="DMR 5G only", dmpUse="surface-object metadata only", imagePlacement=placement["status"],
            sourceLimitations=manifest["coverageLimitations"], blockers=blockers,
            usage="Private processing payload; no release, redistribution, native-resolution or airport approval asserted")
        paths = []
        for filename, report in (("coverage-report.json", coverage), ("transformation-report.json", transformations),
                                 ("seam-report.json", seams), ("height-control-report.json", survey)):
            path = directory / filename
            atomic_json(path, report)
            paths.append(path)
        return paths

    checkpoint.stage("validation-reports", list(source_paths), reports)
    proof = validate_payloads(root, require_acceptance=False)
    proof.update(schemaVersion=SCHEMA, freeBytesBeforeGeneration=free_bytes,
                 minimumFreeBytes=100_000_000_000, executedStages=checkpoint.executed,
                 reusedStages=checkpoint.reused,
                 acceptance=read_json(root / "artifacts/regional-validation/coverage-report.json")["status"],
                 coverageReport="artifacts/regional-validation/coverage-report.json")
    atomic_json(root / "artifacts/regional-validation/load-proof.json", proof)
    return proof


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("stage", "build", "verify-staged", "verify"))
    parser.add_argument("--managed-input", type=Path)
    parser.add_argument("--staging-root", type=Path)
    parser.add_argument("--data-root")
    parser.add_argument("--integrity-only", action="store_true", help="Check storage/topology only; never proves acceptance")
    parser.add_argument("--refresh-evidence", action="store_true", help="Migrate the reviewed v2 checkpoint without regenerating geometry")
    args = parser.parse_args()
    root = staging_root(args.staging_root) if args.command.endswith("staged") or args.command == "stage" else persistent_root(args.data_root)
    if args.command in ("stage", "build"):
        require(args.managed_input, "Materialized source collection is required")
        source_root = staging_root(args.managed_input)
        require(not root.is_relative_to(source_root) and not source_root.is_relative_to(root),
                "Input and output trees must not overlap")
        report = build(source_root, root, refresh_evidence=args.refresh_evidence)
    else:
        report = validate_payloads(root, require_acceptance=not args.integrity_only)
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError, KeyError) as error:
        print(f"Regional processing failed: {error}", file=sys.stderr)
        raise SystemExit(1)
