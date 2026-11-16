"""Plan runner-managed HTTPS acquisition and verify private regional source intake."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import ipaddress
import json
import os
import shutil
import sys
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

REPOSITORY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY / "tools/data_pipeline"))

from validate_sources import REQUIRED_GROUPS, build_report, validate_source


MINIMUM_FREE_BYTES = 100_000_000_000
DEFAULT_CONFIG = REPOSITORY / "Config/Acquisition/ceska-trebova.json"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def atomic_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def digest_file(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def contained(root, logical):
    require(isinstance(logical, str) and logical and "\\" not in logical, "Invalid logical path")
    require(not Path(logical).is_absolute() and ":" not in logical, "Absolute logical path rejected")
    path = (root / logical).resolve()
    require(path.is_relative_to(root.resolve()) and path != root.resolve(), "Logical path escapes persistent root")
    return path


def persistent_root(value):
    configured = value or os.environ.get("FLYING_DATA_ROOT")
    require(configured, "Set FLYING_DATA_ROOT or --data-root to persistent storage outside Git")
    root = Path(configured).resolve()
    require(not root.is_relative_to(REPOSITORY), "Persistent data root must be outside Git")
    return root


def storage_preflight(root, free_bytes=None):
    existing = root
    while not existing.exists():
        existing = existing.parent
    available = shutil.disk_usage(existing).free if free_bytes is None else free_bytes
    require(available >= MINIMUM_FREE_BYTES, f"Acquisition requires at least {MINIMUM_FREE_BYTES} free bytes; got {available}")
    return available


def safe_url(url, domains):
    parsed = urlsplit(url)
    require(parsed.scheme == "https" and parsed.hostname in domains, "HTTPS domain is not configured")
    require(not parsed.username and not parsed.password and not parsed.fragment, "Credential-bearing URL rejected")
    require(parsed.port in (None, 443), "Nonstandard HTTPS port rejected")
    try:
        ipaddress.ip_address(parsed.hostname)
    except ValueError:
        pass
    else:
        raise ValueError("IP literal URL rejected")
    require(not any(key.lower() in {"token", "key", "api_key", "password", "access_token"} for key in parse_qs(parsed.query)), "Credential query rejected")
    return parsed


def canonical_url(url):
    parsed = urlsplit(url)
    return parsed.scheme, parsed.netloc, parsed.path, parse_qs(parsed.query)


def load_config(path=DEFAULT_CONFIG):
    config = read_json(path)
    require(config["schemaVersion"] == "flying.regional-acquisition-config.v1", "Unsupported acquisition configuration")
    require(config["transport"] == "forgemind_native.fetch_https", "Only the managed HTTPS transport is supported")
    region = read_json(contained(REPOSITORY, config["regionManifest"]))
    bounds = region["bounds"]
    require(bounds["crs"] == "EPSG:4326", "Acquisition requires geographic bounds")
    expected = [bounds[key] for key in ("minLonDeg", "minLatDeg", "maxLonDeg", "maxLatDeg")]
    require(expected[0] < expected[2] and expected[1] < expected[3], "Invalid region bounds")
    identities = set()
    destinations = set()
    for source in config["sources"]:
        require(source["kind"] in ("grid", "tiff", "jpeg", "geojson"), "Unsupported payload kind")
        require(source["group"] in (*REQUIRED_GROUPS, "grids", "aeronautical_fallback"), "Unsupported source group")
        parsed = safe_url(source["url"], config["allowedDomains"])
        require(source["id"] not in identities and source["path"] not in destinations, "Duplicate source identity or destination")
        identities.add(source["id"])
        destinations.add(source["path"])
        contained(Path.cwd(), source["path"])
        require(source["maxBytes"] > 0, "Invalid download size limit")
        if source["kind"] == "grid":
            require(parsed.hostname == "cdn.proj.org" and len(source["sha256"]) == 64, "Grid requires published checksum")
            continue
        query = parse_qs(parsed.query)
        envelope = query.get("bbox", query.get("geometry", []))
        require(len(envelope) == 1 and [float(number) for number in envelope[0].split(",")] == expected, "Source request exceeds configured region")
        require(query.get("bboxSR", query.get("inSR")) == ["4326"], "Source envelope must use EPSG:4326")
        if source["kind"] == "geojson":
            require(query.get("geometryType") == ["esriGeometryEnvelope"], "Unbounded feature discovery rejected")
    config["region"] = region
    return config


def validate_payload(path, source):
    with path.open("rb") as stream:
        signature = stream.read(16)
    if source["kind"] in ("tiff", "grid"):
        require(signature[:4] in (b"II*\x00", b"MM\x00*", b"II+\x00", b"MM\x00+"), f"Not a TIFF payload: {path}")
    elif source["kind"] == "jpeg":
        require(signature.startswith(b"\xff\xd8\xff"), f"Not a JPEG payload: {path}")
    elif source["kind"] == "geojson":
        value = read_json(path)
        require(value.get("type") == "FeatureCollection" and isinstance(value.get("features"), list), f"Not a feature payload: {path}")
        require(not value.get("exceededTransferLimit") and not value.get("properties", {}).get("exceededTransferLimit"), f"Truncated query; subdivide configured region: {path}")
        return len(value["features"])
    return None


def verify_receipt(base, source, config):
    payload = contained(base, source["path"])
    receipt = read_json(Path(str(payload) + ".provenance.json"))
    require(canonical_url(receipt["sourceUrl"]) == canonical_url(source["url"]), "Receipt source identity mismatch")
    safe_url(receipt["finalUrl"], config["allowedDomains"])
    require(canonical_url(receipt["finalUrl"]) == canonical_url(source["url"]), "Unexpected redirect; review and configure destination explicitly")
    require(receipt["destinationRelativePath"] == "pilot/" + source["path"], "Receipt destination mismatch")
    require(0 < receipt["sizeBytes"] == payload.stat().st_size <= source["maxBytes"], "Payload size mismatch")
    require(digest_file(payload) == receipt["sha256"], "Payload checksum mismatch")
    if source.get("sha256"):
        require(receipt["sha256"] == source["sha256"], "Published grid checksum mismatch")
    dt.datetime.fromisoformat(receipt["downloadedAt"].replace("Z", "+00:00"))
    return receipt, validate_payload(payload, source)


def plan(config, root):
    available = storage_preflight(root)
    requests = []
    for source in config["sources"]:
        try:
            verify_receipt(root / "data/sources", source, config)
            continue
        except (ValueError, OSError, KeyError):
            pass
        request = {"url": source["url"], "destinationRelativePath": "pilot/" + source["path"], "maxBytes": source["maxBytes"], "minimumFreeSpaceBytes": MINIMUM_FREE_BYTES}
        if source.get("sha256"):
            request["expectedSha256"] = source["sha256"]
        requests.append(request)
    return {"transport": "forgemind_native.fetch_https", "freeBytes": available, "requests": requests}


def assemble(config, base):
    groups = {name: [] for name in (*REQUIRED_GROUPS, "grids", "aeronautical_fallback")}
    findings = []
    for source in config["sources"]:
        receipt, feature_count = verify_receipt(base, source, config)
        crs = {"authority": "EPSG", "code": source["crs"], "name": source["crsName"]}
        record = {
            "id": source["id"], "name": source["name"],
            "version": source.get("version", "sha256:" + receipt["sha256"]),
            "checksum": {"algorithm": "sha256", "path": source["path"], "value": receipt["sha256"]},
            "crs": crs,
            "transform": {"sourceCrs": crs, "targetCrs": crs, "projPipeline": "+proj=noop", "gridFiles": ["none"], "status": "source-native; transformation not performed during acquisition"},
            "provenance": {"publisher": source["publisher"], "sourceUri": receipt["sourceUrl"], "retrievedAtUtc": receipt["downloadedAt"], "packageId": source["id"], "effectiveDate": receipt["downloadedAt"][:10], "effectiveDateBasis": "retrieval snapshot, not a claimed survey or AIRAC effective date", "etag": receipt.get("etag"), "lastModified": receipt.get("lastModified")},
            "sizeBytes": receipt["sizeBytes"], "limitations": source.get("limitations", []),
        }
        if feature_count is not None:
            record["featureCount"] = feature_count
        validate_source(record, source["id"], base, True, findings, require_rights=False)
        groups[source["group"]].append(record)
    manifest = {"schemaVersion": "flying.private-source-acquisition.v1", "manifestVersion": "1", "purpose": "private-non-distributed", "regionId": config["region"]["regionId"], "bounds": config["region"]["bounds"], "requestedMarginM": 0, "sourceGroups": groups, "coverageLimitations": config["coverageLimitations"], "accessAttempts": config["accessAttempts"]}
    report = build_report(Path("data/sources/acquisition-manifest.json"), findings)
    require(report["status"] == "pass", "Source validation failed")
    return manifest, report


def finalize(config, root, staged):
    storage_preflight(root)
    require(staged.resolve().is_relative_to(REPOSITORY / ".forgemind-tmp/managed"), "Materialize managed inputs below .forgemind-tmp/managed")
    manifest, report = assemble(config, staged)
    destination = root / "data/sources"
    for source in config["sources"]:
        for suffix in ("", ".provenance.json"):
            origin = contained(staged, source["path"] + suffix)
            target = contained(destination, source["path"] + suffix)
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_name(target.name + ".tmp")
            shutil.copyfile(origin, temporary)
            temporary.replace(target)
    atomic_json(destination / "acquisition-manifest.json", manifest)
    atomic_json(root / "artifacts/acquisition/source-validation-report.json", report)
    atomic_json(root / "artifacts/acquisition/provenance-manifest.json", manifest)
    verify(config, root)


def verify(config, root):
    manifest, report = assemble(config, root / "data/sources")
    require(read_json(root / "data/sources/acquisition-manifest.json") == manifest, "Acquisition manifest differs from verified collection")
    require(read_json(root / "artifacts/acquisition/provenance-manifest.json") == manifest, "Provenance manifest differs from verified collection")
    saved = read_json(root / "artifacts/acquisition/source-validation-report.json")
    require(saved["status"] == report["status"] == "pass" and saved["summary"]["errorCount"] == 0, "Source-validation report failed")
    return {"status": "pass", "payloadCount": sum(len(group) for group in manifest["sourceGroups"].values()), "coverageLimitations": manifest["coverageLimitations"]}


def staging_root(value):
    require(value is not None, "--staging-root is required for managed publication")
    root = value.resolve()
    parent = REPOSITORY / ".forgemind-tmp/managed"
    require(root.is_relative_to(parent) and root != parent, "Publication staging must be below .forgemind-tmp/managed")
    return root


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("plan", "finalize", "verify", "stage", "verify-staged"))
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--data-root")
    parser.add_argument("--managed-input", type=Path)
    parser.add_argument("--staging-root", type=Path)
    args = parser.parse_args()
    try:
        config = load_config(args.config)
        root = staging_root(args.staging_root) if args.command in ("stage", "verify-staged") else persistent_root(args.data_root)
        if args.command == "plan":
            result = plan(config, root)
        elif args.command in ("finalize", "stage"):
            require(args.managed_input is not None, "--managed-input is required")
            finalize(config, root, args.managed_input)
            result = verify(config, root)
        else:
            result = verify(config, root)
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
