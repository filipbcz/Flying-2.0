"""Technical acceptance for the bounded, non-distributed pilot package only."""

from __future__ import annotations

import math
import shutil
import uuid
from pathlib import Path
from urllib.parse import urlsplit

from acquire_sources import atomic_json, digest_file, read_json, require


def require_real_source(record, payload):
    uri = urlsplit(record["sourceUri"])
    require(uri.scheme == "https" and uri.hostname in {
        "aim.rlp.cz", "www.aeroklubceskatrebova.cz", "ags.cuzk.gov.cz",
        "geoportal.cuzk.gov.cz", "geoportal-zm.cuzk.gov.cz", "cdn.proj.org"},
        "Private acceptance requires an identified real AIM/operator/CUZK source")
    require(record.get("retrievedAt") and not record.get("synthetic", False),
            "Missing acquisition time or synthetic source")
    require(payload.is_file() and digest_file(payload) == record["sha256"],
            f"Private source checksum mismatch: {payload.name}")


def equivalent(actual, expected, label):
    """Allow floating-point transform roundoff, never engineering uncertainty."""
    if isinstance(expected, dict):
        require(isinstance(actual, dict) and actual.keys() == expected.keys(), f"{label}: fields differ")
        for key, value in expected.items():
            equivalent(actual[key], value, f"{label}.{key}")
    elif isinstance(expected, (list, tuple)):
        require(isinstance(actual, (list, tuple)) and len(actual) == len(expected), f"{label}: count differs")
        for index, value in enumerate(expected):
            equivalent(actual[index], value, f"{label}[{index}]")
    elif isinstance(expected, (int, float)) and not isinstance(expected, bool):
        require(isinstance(actual, (int, float)) and not isinstance(actual, bool)
                and math.isfinite(actual) and math.isclose(actual, expected, rel_tol=0, abs_tol=1e-8),
                f"{label}: derived value differs")
    else:
        require(type(actual) is type(expected) and actual == expected, f"{label}: value differs")


def private_package_gate(output):
    reports = output / "artifacts/airport-navigation"
    report_path = reports / "acceptance-report.json"
    try:
        report = _validate(output)
    except (ValueError, OSError, KeyError) as error:
        atomic_json(report_path, dict(status="fail", gate="private-pilot-technical-package",
                    privatePackageAccepted=False, releaseCandidateAccepted=False, reason=str(error)))
        raise
    atomic_json(report_path, report)
    return report


def _validate(output):
    # Imported lazily so the CLI and this module share the existing builders.
    from build_airport_navigation import REGION, build_airport, install, verify
    from import_runways import build_coverage_report

    packages = output / "data/packages"
    root = packages / "regional-airport-navigation"
    reports = output / "artifacts/airport-navigation"
    integrity = verify(root)
    equivalent(read_json(root / "region.json"), read_json(REGION), "unchanged pilot region")

    provenance = read_json(reports / "provenance-report.json")
    require(provenance["privateNonDistributed"] is True and
            provenance["redistributionApprovalClaim"] is False, "Incorrect source-use scope")
    records = provenance["sources"]
    require(len(records) >= 30 and len({r["path"] for r in records}) == len(records),
            "Incomplete or duplicate source inventory")
    from build_airport_navigation import safe_member
    for record in records:
        relative = safe_member(record["path"])
        source_root = root / "sources" if relative.parts[0] == "airport-navigation" else output / "data/sources"
        source = source_root / relative
        require_real_source(record, source)
        sidecar = source.with_name(source.name + ".provenance.json")
        if sidecar.exists():
            acquired = read_json(sidecar)
            require(acquired["sourceUrl"] == record["sourceUri"] and
                    acquired["sha256"] == record["sha256"], "Acquisition provenance differs")

    aero = root / "sources/airport-navigation"
    required_sources = {"aim-index.html", "lkctre.html", "lkuo.html", "lkuo-map.pdf",
                        "ortofoto-airport.png", "ortofoto-export.json", "dmr-airport.tif",
                        "enr-1.html", "enr-2.html", "enr-3.html"}
    require(required_sources <= {r["path"].removeprefix("airport-navigation/") for r in records},
            "Missing required source provenance")
    for path in aero.rglob("*.provenance.json"):
        acquired = read_json(path)
        require_real_source(dict(sourceUri=acquired["sourceUrl"], retrievedAt=acquired["downloadedAt"],
                                 sha256=acquired["sha256"]), path.with_name(path.name.removesuffix(".provenance.json")))

    lineage = read_json(root / "terrain-source-lineage.json")
    require(lineage["purpose"] == "private-non-distributed", "Unexpected terrain source scope")
    for group in lineage["sourceGroups"].values():
        for source in group:
            checksum = source["checksum"]
            require_real_source(dict(sourceUri=source["provenance"]["sourceUri"],
                                     retrievedAt=source["provenance"]["retrievedAtUtc"],
                                     sha256=checksum["value"]),
                                output / "data/sources" / safe_member(checksum["path"]))

    airport, surface, start, derived = build_airport(aero, root / "grids")
    equivalent(read_json(root / "airports.json")["aerodromes"], [airport], "source-derived airport")
    equivalent(read_json(root / "safe-starts.json")["starts"], [start], "source-derived safe start")
    equivalent(read_json(root / "runway-surfaces.json")["surfaces"], [surface], "source-derived surfaces")
    geometry = read_json(root / "geometry-report.json")
    # PROJ's pipeline includes the relocatable grid directory; coordinates and
    # all other derivation/uncertainty fields must still reproduce exactly.
    for key, value in derived.items():
        if key != "transformationPipeline":
            equivalent(geometry[key], value, f"geometry.{key}")
    require(geometry["publishedDimensionsM"] == dict(length=350, width=20)
            and geometry["productionValidated"] is False, "Private geometry incorrectly promoted")
    precision = geometry["precisionAcceptance"]
    require(precision["status"] == "blocked-unmeasured" and precision["measuredErrors"] is None
            and precision["independentControlSource"] is None, "Unsupported survey precision claim")
    require(airport["verificationStatus"] == "unverified" and
            airport["sourcePublishedStatus"] == "Plocha SLZ neověřená", "Operator status not preserved")

    master = read_json(root / "master-list.json")
    database = read_json(root / "airports.json")
    coverage = build_coverage_report(root / "master-list.json", master, database, None)
    require(coverage["passed"] and not any(coverage["missing"].values()), "Private master-list coverage failed")
    stored_coverage = read_json(root / "coverage-review.json")
    require(stored_coverage["approvedMasterList"]["checksumSha256"] == digest_file(root / "master-list.json"),
            "Coverage report is not bound to current master list")
    equivalent(stored_coverage["missing"], coverage["missing"], "coverage missing records")
    catalogue = read_json(root / "project-catalogue.json")
    require(catalogue["allowedMarginM"] == 250 and all(
        r["state"] == "excluded" and r["reason"] and r["distanceToPilotBoundsM"] > 250
        for r in catalogue["excludedRecords"]), "Unexplained or in-margin exclusion")
    require(all(r["reason"] for r in catalogue["nationalIndexDisposition"]), "Unexplained discovered record")

    checksum = read_json(reports / "checksum-report.json")
    archive = packages / safe_member(checksum["archive"])
    require(digest_file(archive) == checksum["sha256"] and archive.stat().st_size == checksum["sizeBytes"],
            "Regional archive checksum mismatch")
    require(digest_file(root / "install-manifest.json") == checksum["manifestSha256"],
            "Installed manifest differs from archive evidence")
    # Reinstall on every invocation: a stale passing relocation report is not proof.
    temporary = output / "relocation-checks" / uuid.uuid4().hex
    temporary.mkdir(parents=True)
    try:
        relocated_root = temporary / "unrelated install path"
        relocation = install(archive, relocated_root)
        require(digest_file(relocated_root / "install-manifest.json") == checksum["manifestSha256"],
                "Relocated archive is a different package")
    finally:
        shutil.rmtree(temporary)
    atomic_json(reports / "relocation-report.json", dict(status="pass", archiveSha256=checksum["sha256"],
                installedUnderUnrelatedPath=True, validation=relocation, sourceCheckoutRequired=False, networkUsed=False))
    return dict(status="pass", gate="private-pilot-technical-package", privatePackageAccepted=True,
                privateNonDistributed=True, releaseCandidateAccepted=False, productionGeometryAccepted=False,
                geometryStatus="derived-unverified-private", sourcePublishedStatus=airport["sourcePublishedStatus"],
                sourceRecordsChecked=len(records), packageIntegrity=integrity,
                coverage=dict(missing=coverage["missing"], nationalCompletenessClaim=False),
                archiveSha256=checksum["sha256"], relocation="pass", runtimeNetworkRequired=False,
                regionChanged=False, surveyPrecisionRequiredForThisGate=False,
                productionGate=dict(command="production-gate", status="blocked-unmeasured"),
                limitations=["Derived endpoints are not surveyed physical thresholds",
                    "Unverified SLZ; opt-in static mesh start only",
                    "Nominal advisory ATZ; no operational airspace completeness",
                    "No two-airport flight, Unreal flight or release acceptance claim"])
