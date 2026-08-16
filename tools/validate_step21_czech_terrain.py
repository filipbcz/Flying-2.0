#!/usr/bin/env python3
"""Validate that the Czech terrain gate fails closed without authoritative inputs."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
TERRAIN_MANIFEST = REPO_ROOT / "Data" / "Terrain" / "CZManifest.json"
VISUAL_MANIFEST = REPO_ROOT / "Data" / "Visual" / "CZManifest.json"
COVERAGE_REPORT = REPO_ROOT / "Reports" / "terrain_coverage.json"
BLOCKERS = REPO_ROOT / "docs" / "blockers" / "external-inputs.yml"
FORBIDDEN_SUCCESS_MARKERS = (
    '"completeWithinDeclaredBounds": true',
    '"missingRequiredTiles": []',
    '"interruptionFreeWithinDeclaredBounds": true',
    '"passed": true'
)


def load_json(path: Path) -> dict[str, Any]:
    with path.open(encoding="utf-8") as handle:
        value = json.load(handle)
    if not isinstance(value, dict):
        raise ValueError(f"{path.relative_to(REPO_ROOT)} must contain a JSON object")
    return value


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def blocker_ids() -> set[str]:
    ids: set[str] = set()
    for line in BLOCKERS.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("- blocker_id:"):
            ids.add(line.split(":", 1)[1].strip().strip('"'))
    return ids


def validate_blocker_registry() -> None:
    ids = blocker_ids()
    require(
        "national-cuzk-production-source-package" in ids,
        "missing blocker national-cuzk-production-source-package",
    )
    require(
        "national-terrain-runtime-streaming-evidence" in ids,
        "missing blocker national-terrain-runtime-streaming-evidence",
    )


def validate_blocked_manifest(path: Path, manifest: dict[str, Any]) -> None:
    rel = path.relative_to(REPO_ROOT).as_posix()
    require(manifest.get("status") == "blocked", f"{rel} must be blocked")
    require(manifest.get("blockerRef", "").startswith("docs/blockers/external-inputs.yml#"), f"{rel} must link blocker registry")
    require(manifest.get("coverage", {}).get("scope") == "czech-republic", f"{rel} must remain scoped to Czech Republic")
    require(manifest.get("coverage", {}).get("completeWithinDeclaredBounds") is False, f"{rel} must not claim complete national coverage")
    require(not manifest.get("sourceLineage"), f"{rel} must not claim source lineage without source payloads")
    serialized = json.dumps(manifest, sort_keys=True)
    for marker in FORBIDDEN_SUCCESS_MARKERS:
        require(marker not in serialized, f"{rel} contains forbidden success marker {marker}")


def validate_terrain_manifest(manifest: dict[str, Any]) -> None:
    validate_blocked_manifest(TERRAIN_MANIFEST, manifest)
    require(manifest.get("renderLods") == [], "terrain manifest must not list synthetic render tiles")
    require(manifest.get("collisionTiles") == [], "terrain manifest must not list synthetic collision tiles")
    points = manifest.get("validation", {}).get("controlPoints", {}).get("points")
    require(points == [], "terrain manifest must not embed self-reported control points")
    require(
        manifest.get("validation", {}).get("controlPoints", {}).get("status") == "blocked",
        "terrain control-point validation must be blocked",
    )


def validate_visual_manifest(manifest: dict[str, Any]) -> None:
    validate_blocked_manifest(VISUAL_MANIFEST, manifest)
    require(manifest.get("imagery", {}).get("lods") == [], "visual manifest must not list synthetic imagery tiles")
    require(manifest.get("vectorPackages", {}).get("layers") == [], "visual manifest must not list synthetic vector layers")
    require(manifest.get("packagingLayout", {}).get("totalBytes") == 0, "visual package bytes must be zero while blocked")


def validate_coverage_report(report: dict[str, Any]) -> None:
    require(report.get("status") == "blocked", "coverage report must be blocked")
    refs = set(report.get("blockerRefs", []))
    require(
        "docs/blockers/external-inputs.yml#national-cuzk-production-source-package" in refs,
        "coverage report must link national source blocker",
    )
    require(
        "docs/blockers/external-inputs.yml#national-terrain-runtime-streaming-evidence" in refs,
        "coverage report must link runtime streaming blocker",
    )
    require(report.get("requiredTileIndex") is None, "coverage report must not name a self-authored tile index")
    require(report.get("missingRequiredTiles") is None, "coverage report must not claim zero missing tiles")
    require(report.get("tileCoverage", {}).get("completeWithinDeclaredBounds") is False, "coverage report must not claim complete bounds")
    require(report.get("packageBoundaryStreamingEvidence") is None, "coverage report must not claim manual streaming evidence")
    require(report.get("validation", {}).get("passed") is False, "coverage report must not pass")
    blocked = {entry.get("id") for entry in report.get("blockedInputs", [])}
    for required in (
        "authoritative-cz-boundary-tile-index",
        "official-cuzk-national-source-manifest",
        "offline-runtime-boundary-streaming-log",
    ):
        require(required in blocked, f"coverage report missing blocked input {required}")


def main() -> int:
    try:
        validate_blocker_registry()
        validate_terrain_manifest(load_json(TERRAIN_MANIFEST))
        validate_visual_manifest(load_json(VISUAL_MANIFEST))
        validate_coverage_report(load_json(COVERAGE_REPORT))
        print("PASS Czech Republic terrain gate is blocked on missing authoritative production inputs")
        return 0
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"validate_step21_czech_terrain: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
