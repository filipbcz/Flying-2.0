"""Bounded managed acquisition and offline binding of CUZK regional evidence."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import shutil
import sys
from urllib.parse import parse_qs, urlencode, urlsplit, urlunsplit

import numpy as np
import pyproj

from acquire_sources import (
    MINIMUM_FREE_BYTES, atomic_json, canonical_url, contained, digest_file,
    load_config, read_json, require, safe_url, staging_root, storage_preflight,
)


ZVBP = "https://ags.cuzk.gov.cz/arcgis/rest/services/BodovaPole/MapServer/44"
DMR_SPEC = "https://ags.cuzk.gov.cz/arcgis2/rest/services/dmr5g/ImageServer"
EVIDENCE_PATHS = ("ortofoto/export-placement.json", "validation/height-controls.json")
EVIDENCE_MANIFEST = "regional-evidence-manifest.json"


def manifest_digest(manifest):
    return hashlib.sha256((json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")).hexdigest()


def evidence_root(source_root):
    if (source_root / EVIDENCE_MANIFEST).is_file():
        return source_root
    normalized = source_root.parent / "normalized"
    if source_root.name == "sources" and (normalized / EVIDENCE_MANIFEST).is_file():
        return normalized
    return source_root


def acquisition_manifest(source_root):
    path = source_root / "acquisition-manifest.json"
    manifest = read_json(path)
    overlay = evidence_root(source_root) / EVIDENCE_MANIFEST
    if overlay.is_file():
        record = read_json(overlay)
        require(record["schemaVersion"] == "flying.regional-evidence-manifest.v1" and
                record["acquisitionManifestSha256"] == digest_file(path) and "evidence" not in manifest,
                "Evidence overlay does not match immutable acquisition manifest")
        manifest["evidence"] = record["evidence"]
    return manifest


def requests(config):
    image = next(source for source in config["sources"] if source["group"] == "ortofoto")
    parsed = urlsplit(image["url"])
    query = parse_qs(parsed.query)
    require(query.pop("f") == ["image"], "Expected configured image export")
    query["f"] = ["pjson"]
    metadata = urlunsplit(parsed._replace(query=urlencode(query, doseq=True)))
    bounds = config["region"]["bounds"]
    envelope = ",".join(str(bounds[key]) for key in ("minLonDeg", "minLatDeg", "maxLonDeg", "maxLatDeg"))
    controls = ZVBP + "/query?" + urlencode(dict(
        where="1=1", geometry=envelope, geometryType="esriGeometryEnvelope", inSR=4326,
        spatialRel="esriSpatialRelIntersects", outFields="*", outSR=5514,
        returnGeometry="true", f="pjson"))
    return [dict(path="ortofoto/export-response.json", url=metadata, maxBytes=1_000_000,
                 evidenceKind="cuzk-export-response"),
            dict(path="validation/zvbp-points.json", url=controls, maxBytes=4_000_000,
                 evidenceKind="cuzk-zvbp-layer-44")]


def verified_download(root, source, config):
    path = contained(root, source["path"])
    receipt = read_json(Path(str(path) + ".provenance.json"))
    for key in ("sourceUrl", "finalUrl"):
        safe_url(receipt[key], config["allowedDomains"])
        require(canonical_url(receipt[key]) == canonical_url(source["url"]), "Evidence receipt URL mismatch")
    require(receipt["destinationRelativePath"] == "pilot-evidence/" + source["path"],
            "Evidence receipt destination mismatch")
    require(0 < receipt["sizeBytes"] == path.stat().st_size <= source["maxBytes"], "Evidence size mismatch")
    require(receipt["sha256"] == digest_file(path), "Evidence response checksum mismatch")
    dt.datetime.fromisoformat(receipt["downloadedAt"].replace("Z", "+00:00"))
    value = read_json(path)
    require(not value.get("error") and not value.get("exceededTransferLimit"), "Failed or truncated evidence response")
    return value, receipt


def placement_record(response, receipt, image, source_root):
    require(digest_file(contained(source_root, image["checksum"]["path"])) == image["checksum"]["value"],
            "Acquired image checksum mismatch")
    request = urlsplit(image["provenance"]["sourceUri"])
    query = parse_qs(request.query)
    query["f"] = ["pjson"]
    require(canonical_url(urlunsplit(request._replace(query=urlencode(query, doseq=True)))) ==
            canonical_url(receipt["sourceUrl"]), "Metadata is not from the acquired image request")
    safe_url(response["href"], ["ags.cuzk.gov.cz"])
    require(urlsplit(response["href"]).path.startswith("/arcgis1/rest/directories/arcgisoutput/ORTOFOTO_MapServer/"),
            "Unexpected orthophoto response URL")
    require(isinstance(response["width"], int) and isinstance(response["height"], int) and
            response["width"] > 0 and response["height"] > 0, "Invalid returned image dimensions")
    extent = response["extent"]
    bounds = [float(extent[key]) for key in ("xmin", "ymin", "xmax", "ymax")]
    require(np.isfinite(bounds).all() and bounds[0] < bounds[2] and bounds[1] < bounds[3],
            "Invalid returned extent")
    require((extent["spatialReference"].get("latestWkid") or extent["spatialReference"].get("wkid")) == 4326,
            "Unexpected returned orthophoto CRS")
    return dict(schemaVersion="flying.export-placement.v1", evidenceKind="returned-export-response",
                imageSha256=image["checksum"]["value"], sourceUri=image["provenance"]["sourceUri"],
                width=response["width"], height=response["height"], extent=extent, href=response["href"],
                responsePath="ortofoto/export-response.json", responseSha256=receipt["sha256"],
                metadataSourceUri=receipt["sourceUrl"], retrievedAtUtc=receipt["downloadedAt"],
                imageRetrievedAtUtc=image["provenance"]["retrievedAtUtc"],
                binding="Same configured export parameters except f=pjson; local acquired JPEG SHA-256. "
                        "Metadata is a later service snapshot, not proof of byte identity with the temporary href.")


def control_record(response, receipt, terrain, config):
    require(response.get("geometryType") == "esriGeometryPoint" and response.get("features"),
            "Expected nonempty ZVBP point response")
    spatial = response["spatialReference"]
    require((spatial.get("latestWkid") or spatial.get("wkid")) == 5514, "ZVBP coordinates must be S-JTSK east/north")
    bounds = config["region"]["bounds"]
    to_geographic = pyproj.Transformer.from_crs(5514, 4326, always_xy=True, allow_ballpark=False)
    points = []
    excluded = []
    for feature in response["features"]:
        attributes, geometry = feature["attributes"], feature["geometry"]
        east, north = float(geometry["x"]), float(geometry["y"])
        height = float(attributes["VYSKA"])
        require(np.isfinite([east, north, height]).all(), "Missing or invalid ZVBP coordinates/height")
        require(abs(east + float(attributes["Y"])) <= .02 and abs(north + float(attributes["X"])) <= .02,
                "ZVBP coordinate attributes disagree with S-JTSK geometry")
        longitude, latitude = to_geographic.transform(east, north)
        require(attributes["CISLO"] and attributes["GEODETICKE_UDAJE"], "Missing point identity or survey reference")
        # Retain the complete returned attributes, including control identifiers
        # and the literal mark-type query code. Do not infer physical placement
        # (wall, ground, etc.) from an undocumented code or a small residual.
        mark_type = parse_qs(urlsplit(attributes["GEODETICKE_UDAJE"]).query).get("TYP", [None])[0]
        point = dict(id=attributes["CISLO"], objectId=attributes["OBJECTID"],
                     eastM=east, northM=north, baltic1957M=height,
                     sourceUri=receipt["sourceUrl"], surveyUri=attributes["GEODETICKE_UDAJE"],
                     rawHeight=attributes["VYSKA"], sourceAttributes=attributes,
                     controlType="CUZK ZVBP layer 44", controlOrder=attributes.get("PORAD"),
                     markType=mark_type, markTypeSource="GEODETICKE_UDAJE query parameter TYP",
                     physicalMarkType=None, controlUncertaintyM=None, markToGroundOffsetM=None,
                     groundRelationship=None,
                     comparability=dict(status="not-comparable",
                         reason="Layer 44 provides a Bpv control-mark height but no documented "
                                "relationship or offset to the DMR bare-earth surface."))
        if not (bounds["minLonDeg"] <= longitude <= bounds["maxLonDeg"] and
                bounds["minLatDeg"] <= latitude <= bounds["maxLatDeg"]):
            excluded.append(dict(point, reason="Outside configured geographic pilot polygon; projected envelope-query fringe"))
            continue
        points.append(point)
    require(len(points) >= 3 and len({point["id"] for point in points}) == len(points), "Insufficient or duplicate ZVBP points")
    return dict(schemaVersion="flying.height-controls.v2", evidenceKind="cuzk-zvbp-layer-44",
                crs="EPSG:5514+8357", independentSurvey=True,
                terrainSha256=terrain["checksum"]["value"],
                provenance=dict(sourceUri=receipt["sourceUrl"], surveyId="CUZK-ZVBP-layer-44",
                                retrievedAtUtc=receipt["downloadedAt"], responsePath="validation/zvbp-points.json",
                                responseSha256=receipt["sha256"]),
                queryBounds=bounds, requestedMarginM=0,
                allowedMarginM=config["region"]["sourceScope"]["allowedMarginM"],
                sourceUncertaintyM=.30,
                uncertaintyProvenance=dict(sourceUri=DMR_SPEC, appliesTo="DMR 5G source terrain, not ZVBP benchmarks",
                    statistic="Complete mean height error, not a pointwise maximum bound",
                    openTerrainM=.18, forestTerrainM=.30,
                    policy="Use documented larger DMR source uncertainty; never infer ZVBP uncertainty from decimal precision"),
                controlUncertaintyM=None, markToGroundOffsetM=None,
                sourceFieldDefinitions=response.get("fields", []),
                sourceFieldAliases=response.get("fieldAliases", {}),
                fieldAvailability=dict(perPointSurveyUncertainty="not-provided",
                                       markToGroundOffset="not-provided",
                                       physicalMarkType="not-provided",
                                       markTypeCode="GEODETICKE_UDAJE query parameter TYP, when present"),
                datasetAcceptance=dict(maximumAbsoluteGroundResidualM=.40,
                    basis="Dataset policy: documented larger DMR mean height error (0.30 m) plus 0.10 m.",
                    appliesTo="Only controls with a documented ground relationship; not raw levelling-mark residuals",
                    noComparableControls="Report independent plausibility only; ground accuracy remains unassessed"),
                limitations=["Layer 44 does not expose per-point absolute survey uncertainty or ground-to-mark offsets.",
                             "VYSKA is the Bpv height of a levelling mark, which may be on a wall or structure; "
                             "it is not automatically bare-earth height. No offset is fabricated and no controls are filtered by residual.",
                             "DMR source accuracy does not guarantee accuracy of the 2048-square service-resampled export."],
                groundEquivalenceVerified=False, points=points, spatiallyExcluded=excluded,
                returnedPointCount=len(response["features"]),
                selectionPolicy="Geographic pilot polygon only; never selected by terrain height or residual")


def stage(evidence_root, source_root, config):
    storage_preflight(source_root)
    manifest_path = source_root / "acquisition-manifest.json"
    manifest = read_json(manifest_path)
    downloads = requests(config)
    verified = [verified_download(evidence_root, source, config) for source in downloads]
    placement = placement_record(*verified[0], manifest["sourceGroups"]["ortofoto"][0], source_root)
    controls = control_record(*verified[1], manifest["sourceGroups"]["dmr"][0], config)
    evidence = []
    for source, (_, receipt) in zip(downloads, verified):
        for suffix in ("", ".provenance.json"):
            destination = contained(source_root, source["path"] + suffix)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(contained(evidence_root, source["path"] + suffix), destination)
        evidence.append(dict(kind=source["evidenceKind"], path=source["path"], sha256=receipt["sha256"],
                             sourceUri=receipt["sourceUrl"], retrievedAtUtc=receipt["downloadedAt"]))
    for path, record in zip(EVIDENCE_PATHS, (placement, controls)):
        atomic_json(source_root / path, record)
        evidence.append(dict(kind=record["evidenceKind"], path=path, sha256=digest_file(source_root / path)))
    manifest.pop("evidence", None)
    atomic_json(manifest_path, manifest)
    atomic_json(source_root / EVIDENCE_MANIFEST,
                dict(schemaVersion="flying.regional-evidence-manifest.v1",
                     acquisitionManifestSha256=digest_file(manifest_path), evidence=evidence))
    return verify(source_root, config)


def verify(source_root, config):
    manifest = acquisition_manifest(source_root)
    evidence = evidence_root(source_root)
    for record in manifest["evidence"]:
        require(digest_file(contained(evidence, record["path"])) == record["sha256"], "Evidence manifest checksum mismatch")
    verified = [verified_download(evidence, source, config) for source in requests(config)]
    expected = (placement_record(*verified[0], manifest["sourceGroups"]["ortofoto"][0], source_root),
                control_record(*verified[1], manifest["sourceGroups"]["dmr"][0], config))
    for path, record in zip(EVIDENCE_PATHS, expected):
        require(read_json(evidence / path) == record, "Derived evidence differs from checksum-bound CUZK response")
    expected_manifest = [dict(kind=source["evidenceKind"], path=source["path"], sha256=receipt["sha256"],
                              sourceUri=receipt["sourceUrl"], retrievedAtUtc=receipt["downloadedAt"])
                         for source, (_, receipt) in zip(requests(config), verified)]
    expected_manifest.extend(dict(kind=record["evidenceKind"], path=path, sha256=digest_file(evidence / path))
                             for path, record in zip(EVIDENCE_PATHS, expected))
    require(manifest["evidence"] == expected_manifest, "Incomplete or inconsistent evidence manifest")
    return dict(status="pass", validationKind="evidence-intake-not-height-acceptance", controlCount=len(expected[1]["points"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "stage", "verify"))
    parser.add_argument("--evidence-root", type=Path)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args()
    config = load_config()
    if args.command == "plan":
        result = [dict(url=source["url"], destinationRelativePath="pilot-evidence/" + source["path"],
                       maxBytes=source["maxBytes"], minimumFreeSpaceBytes=MINIMUM_FREE_BYTES) for source in requests(config)]
    else:
        root = staging_root(args.source_root)
        result = stage(staging_root(args.evidence_root), root, config) if args.command == "stage" else verify(root, config)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(f"Regional evidence failed: {error}", file=sys.stderr)
        raise SystemExit(1)
