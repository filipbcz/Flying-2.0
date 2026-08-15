#!/usr/bin/env python3
"""Import source-aware aerodrome runway records and emit coverage/AIRAC reports."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any


ACTIVE_CLASSES = {"active_airport", "slz_field"}
ACTIVE_STATUS = {"active"}
SCHEMA_VERSION = "flying.aerodrome-database.v1"
COVERAGE_SCHEMA_VERSION = "flying.airport-coverage-report.v1"
LEGACY_SCHEMA_VERSION = "flying.airport-database.v1"


class ImportErrorWithReport(Exception):
    def __init__(self, report: dict[str, Any]) -> None:
        super().__init__("runway import failed")
        self.report = report


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


def repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def resolve_schema_ref(schema: dict[str, Any], ref: str) -> dict[str, Any]:
    if not ref.startswith("#/"):
        raise ValueError(f"unsupported schema reference {ref}")
    node: Any = schema
    for part in ref[2:].split("/"):
        node = require_dict(node, ref)[part]
    return require_dict(node, ref)


def validate_json_schema_subset(value: Any, schema_node: dict[str, Any], root_schema: dict[str, Any], path: str) -> list[str]:
    if "$ref" in schema_node:
        return validate_json_schema_subset(value, resolve_schema_ref(root_schema, str(schema_node["$ref"])), root_schema, path)

    errors: list[str] = []
    if "const" in schema_node and value != schema_node["const"]:
        errors.append(f"{path} must equal {schema_node['const']!r}")
    if "enum" in schema_node and value not in schema_node["enum"]:
        errors.append(f"{path} must be one of {schema_node['enum']!r}")

    expected_type = schema_node.get("type")
    if expected_type is not None:
        allowed = expected_type if isinstance(expected_type, list) else [expected_type]
        if not any(schema_type_matches(value, item) for item in allowed):
            errors.append(f"{path} has invalid type")
            return errors

    if isinstance(value, str):
        if "minLength" in schema_node and len(value) < int(schema_node["minLength"]):
            errors.append(f"{path} is shorter than minLength")
        if "pattern" in schema_node:
            import re

            if re.match(str(schema_node["pattern"]), value) is None:
                errors.append(f"{path} does not match pattern")
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        if "minimum" in schema_node and value < float(schema_node["minimum"]):
            errors.append(f"{path} is below minimum")
        if "maximum" in schema_node and value > float(schema_node["maximum"]):
            errors.append(f"{path} is above maximum")
        if "exclusiveMinimum" in schema_node and value <= float(schema_node["exclusiveMinimum"]):
            errors.append(f"{path} is below or equal to exclusiveMinimum")
        if "exclusiveMaximum" in schema_node and value >= float(schema_node["exclusiveMaximum"]):
            errors.append(f"{path} is above or equal to exclusiveMaximum")
    if isinstance(value, list):
        if "minItems" in schema_node and len(value) < int(schema_node["minItems"]):
            errors.append(f"{path} has too few items")
        if "maxItems" in schema_node and len(value) > int(schema_node["maxItems"]):
            errors.append(f"{path} has too many items")
        if "items" in schema_node:
            item_schema = require_dict(schema_node["items"], f"{path}.items")
            for index, item in enumerate(value):
                errors.extend(validate_json_schema_subset(item, item_schema, root_schema, f"{path}[{index}]"))
    if isinstance(value, dict):
        required_fields = schema_node.get("required", [])
        if isinstance(required_fields, list):
            for field in required_fields:
                if field not in value:
                    errors.append(f"{path}.{field} is required")
        properties = schema_node.get("properties", {})
        if isinstance(properties, dict):
            if schema_node.get("additionalProperties") is False:
                for field in value:
                    if field not in properties:
                        errors.append(f"{path}.{field} is not allowed")
            for field, property_schema in properties.items():
                if field in value:
                    errors.extend(
                        validate_json_schema_subset(
                            value[field],
                            require_dict(property_schema, f"{path}.{field}.schema"),
                            root_schema,
                            f"{path}.{field}",
                        )
                    )
        for item in schema_node.get("allOf", []):
            item_schema = require_dict(item, f"{path}.allOf")
            if "if" in item_schema and "then" in item_schema:
                if not validate_json_schema_subset(value, require_dict(item_schema["if"], f"{path}.if"), root_schema, path):
                    errors.extend(validate_json_schema_subset(value, require_dict(item_schema["then"], f"{path}.then"), root_schema, path))
            else:
                errors.extend(validate_json_schema_subset(value, item_schema, root_schema, path))
    return errors


def schema_type_matches(value: Any, expected_type: str) -> bool:
    if expected_type == "null":
        return value is None
    if expected_type == "boolean":
        return isinstance(value, bool)
    if expected_type == "object":
        return isinstance(value, dict)
    if expected_type == "array":
        return isinstance(value, list)
    if expected_type == "string":
        return isinstance(value, str)
    if expected_type == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected_type == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    return False


def validate_against_schema(value: Any, schema_path: Path, artifact_name: str) -> None:
    schema = require_dict(read_json(schema_path), str(schema_path))
    errors = validate_json_schema_subset(value, schema, schema, "$")
    if errors:
        raise ValueError(f"{artifact_name} failed {schema_path}: " + "; ".join(errors[:8]))


def provenance_with_checksums(items: list[Any], fallback_effective_date: str) -> list[dict[str, Any]]:
    normalized: list[dict[str, Any]] = []
    for index, item in enumerate(items):
        source = copy.deepcopy(require_dict(item, f"provenance[{index}]"))
        source_id = str(required(source, "sourceId", f"provenance[{index}]"))
        source.setdefault("effectiveDate", fallback_effective_date)
        source.setdefault(
            "checksum",
            {
                "algorithm": "sha256",
                "value": hashlib.sha256(json.dumps(source, sort_keys=True).encode("utf-8")).hexdigest(),
            },
        )
        normalized.append(source)
        if source.get("permissionStatus") != "permitted":
            raise ValueError(f"source {source_id} is not permitted for production import")
    if not normalized:
        raise ValueError("at least one provenance/sourceAttribution record is required")
    return normalized


def physical_threshold(end: dict[str, Any], path: str) -> dict[str, Any]:
    threshold = require_dict(required(end, "threshold", path), f"{path}.threshold")
    physical = require_dict(
        required(threshold, "physicalThreshold", f"{path}.threshold"),
        f"{path}.threshold.physicalThreshold",
    )
    derived = threshold.get("approvedDerivedMeasurement")
    geometry_source = threshold.get("geometrySource", "threshold_geometry")
    if geometry_source == "threshold_geometry":
        if "positionWgs84" in physical:
            return physical
        raise ValueError(f"{path}.threshold.physicalThreshold.positionWgs84 is required for threshold geometry")
    if geometry_source == "approved_derived_measurement":
        derived_record = require_dict(
            required(threshold, "approvedDerivedMeasurement", f"{path}.threshold"),
            f"{path}.threshold.approvedDerivedMeasurement",
        )
        if derived_record.get("approved") is not True:
            raise ValueError(f"{path}.threshold.approvedDerivedMeasurement.approved must be true")
        for field in ("method", "reviewer", "reviewedAtUtc", "sourceAttribution"):
            required(derived_record, field, f"{path}.threshold.approvedDerivedMeasurement")
        provenance_with_checksums(
            require_list(
                derived_record["sourceAttribution"],
                f"{path}.threshold.approvedDerivedMeasurement.sourceAttribution",
            ),
            str(derived_record.get("reviewedAtUtc", "1970-01-01"))[:10],
        )
        if "positionWgs84" in physical:
            return physical
    raise ValueError(
        f"{path}.threshold.geometrySource must be threshold_geometry or approved_derived_measurement with approved metadata"
    )


def normalized_runway(aerodrome: dict[str, Any], runway: dict[str, Any], index: int) -> dict[str, Any]:
    runway_id = str(required(runway, "id", f"runways[{index}]"))
    runway_path = f"aerodromes[{aerodrome.get('id')}].runways[{runway_id}]"
    airac_effective = str(required(runway, "airacEffectiveDate", runway_path))
    ends = require_list(required(runway, "ends", runway_path), f"{runway_path}.ends")
    if len(ends) != 2:
        raise ValueError(f"{runway_path}.ends must contain exactly two runway ends")
    normalized_ends = []
    for end_index, end_value in enumerate(ends):
        end = copy.deepcopy(require_dict(end_value, f"{runway_path}.ends[{end_index}]"))
        physical_threshold(end, f"{runway_path}.ends[{end_index}]")
        threshold = require_dict(end["threshold"], f"{runway_path}.ends[{end_index}].threshold")
        threshold.setdefault("geometrySource", "threshold_geometry")
        if threshold["geometrySource"] not in {"threshold_geometry", "approved_derived_measurement"}:
            raise ValueError(f"{runway_path}.ends[{end_index}].threshold.geometrySource is invalid")
        end["sourceAttribution"] = provenance_with_checksums(
            list(end.get("sourceAttribution", end.get("provenance", []))),
            airac_effective,
        )
        end.pop("provenance", None)
        normalized_ends.append(end)

    output = copy.deepcopy(runway)
    output["sourceAttribution"] = provenance_with_checksums(
        list(output.get("sourceAttribution", output.get("provenance", []))),
        airac_effective,
    )
    output.pop("provenance", None)
    output.setdefault("geometryReview", {"manualOverwriteRequiresReview": False})
    output["ends"] = normalized_ends
    return output


def normalized_database(master: dict[str, Any]) -> dict[str, Any]:
    airac = require_dict(required(master, "airac", "$"), "$.airac")
    aerodromes = []
    for index, value in enumerate(require_list(required(master, "aerodromes", "$"), "$.aerodromes")):
        aerodrome = copy.deepcopy(require_dict(value, f"aerodromes[{index}]"))
        effective_date = str(required(aerodrome, "airacEffectiveDate", f"aerodromes[{index}]"))
        aerodrome["sourceAttribution"] = provenance_with_checksums(
            list(aerodrome.get("sourceAttribution", aerodrome.get("provenance", []))),
            effective_date,
        )
        aerodrome.pop("provenance", None)
        aerodrome["runways"] = [
            normalized_runway(aerodrome, require_dict(runway, "runway"), runway_index)
            for runway_index, runway in enumerate(aerodrome.get("runways", []))
        ]
        aerodromes.append(aerodrome)
    return {
        "schemaVersion": SCHEMA_VERSION,
        "databaseVersion": str(master.get("databaseVersion", airac.get("cycleId", "unknown"))),
        "airac": airac,
        "aerodromes": aerodromes,
    }


def active_master_records(master: dict[str, Any]) -> list[dict[str, Any]]:
    records = require_list(required(master, "masterList", "$"), "$.masterList")
    active = []
    for item in records:
        record = require_dict(item, "masterList[]")
        if record.get("classification") in ACTIVE_CLASSES and record.get("operationalStatus") in ACTIVE_STATUS:
            active.append(record)
    return active


def runway_index(database: dict[str, Any]) -> dict[str, dict[str, Any]]:
    indexed: dict[str, dict[str, Any]] = {}
    for aerodrome in database["aerodromes"]:
        for runway in aerodrome.get("runways", []):
            indexed[f"{aerodrome['id']}/{runway['id']}"] = runway
    return indexed


def runway_is_manually_verified(runway: dict[str, Any]) -> bool:
    validation = runway.get("validation", {})
    manual = validation.get("manualVerification", {}) if isinstance(validation, dict) else {}
    return bool(runway.get("geometryReview", {}).get("manualOverwriteRequiresReview")) or validation.get(
        "status"
    ) in {
        "manually_verified",
        "production_validated",
    } or manual.get("status") == "reviewer_approved"


def diff_records(previous: dict[str, Any] | None, current: dict[str, Any]) -> list[dict[str, Any]]:
    if previous is None:
        return []
    old = runway_index(previous)
    new = runway_index(current)
    changes: list[dict[str, Any]] = []
    for key in sorted(new.keys() - old.keys()):
        changes.append({"id": key, "changeType": "added", "requiresManualReview": False})
    for key in sorted(old.keys() - new.keys()):
        changes.append({"id": key, "changeType": "removed", "requiresManualReview": True})
    for key in sorted(old.keys() & new.keys()):
        fields = []
        for field in ("designator", "surface", "dimensionsM", "lighting", "markings", "slope", "ends"):
            if old[key].get(field) != new[key].get(field):
                fields.append(field)
        if fields:
            requires_review = runway_is_manually_verified(old[key])
            changes.append(
                {
                    "id": key,
                    "changeType": "modified",
                    "fields": fields,
                    "requiresManualReview": requires_review,
                }
            )
    return changes


def expected_master_runways(master: dict[str, Any], active_records: list[dict[str, Any]]) -> dict[str, dict[str, Any]]:
    active_ids = {str(record["aerodromeId"]) for record in active_records}
    expected: dict[str, dict[str, Any]] = {}
    for aerodrome in require_list(master.get("aerodromes", []), "$.aerodromes"):
        aerodrome_record = require_dict(aerodrome, "$.aerodromes[]")
        aerodrome_id = str(aerodrome_record.get("id", ""))
        if aerodrome_id not in active_ids:
            continue
        for runway in require_list(aerodrome_record.get("runways", []), f"$.aerodromes[{aerodrome_id}].runways"):
            runway_record = require_dict(runway, f"$.aerodromes[{aerodrome_id}].runways[]")
            runway_id = str(required(runway_record, "id", f"$.aerodromes[{aerodrome_id}].runways[]"))
            expected[f"{aerodrome_id}/{runway_id}"] = {
                "aerodromeId": aerodrome_id,
                "runwayId": runway_id,
                "designator": str(runway_record.get("designator", "")),
            }
    return expected


def build_coverage_report(
    master_path: Path,
    master: dict[str, Any],
    database: dict[str, Any],
    previous: dict[str, Any] | None,
) -> dict[str, Any]:
    active_records = active_master_records(master)
    imported_by_id = {aerodrome["id"]: aerodrome for aerodrome in database["aerodromes"]}
    imported_runways = runway_index(database)
    expected_runways = expected_master_runways(master, active_records)
    missing_aerodromes = []
    missing_slz = []
    missing_runways = []
    for record in active_records:
        record_id = str(record["aerodromeId"])
        imported = imported_by_id.get(record_id)
        if imported is None:
            item = {"id": record_id, "name": str(record["name"]), "reason": "active master-list record not imported"}
            if record.get("classification") == "slz_field":
                missing_slz.append(item)
            else:
                missing_aerodromes.append(item)
            continue
        if not imported.get("runways"):
            missing_runways.append(
                {"id": record_id, "name": str(record["name"]), "reason": "active imported aerodrome has no runway records"}
            )
    for expected_id, expected in sorted(expected_runways.items()):
        if expected_id not in imported_runways:
            missing_runways.append(
                {
                    "id": expected_id,
                    "name": expected["runwayId"],
                    "aerodromeId": expected["aerodromeId"],
                    "designator": expected["designator"],
                    "reason": "master-list runway is not present in imported runway records",
                }
            )

    changes = diff_records(previous, database)
    blocked = [
        dict(change, changeType="manual_geometry_overwrite_blocked")
        for change in changes
        if change.get("requiresManualReview")
    ]
    report = {
        "schemaVersion": COVERAGE_SCHEMA_VERSION,
        "generatedAtUtc": now_utc(),
        "scope": master.get("metadata", {}).get("scope", "national_airport_master_list"),
        "airac": database["airac"],
        "approvedMasterList": {
            "sourcePath": str(master_path),
            "checksumSha256": sha256_file(master_path),
        },
        "counts": {
            "masterListActiveAerodromes": sum(1 for item in active_records if item.get("classification") == "active_airport"),
            "masterListActiveSlzAreas": sum(1 for item in active_records if item.get("classification") == "slz_field"),
            "importedAerodromes": len(database["aerodromes"]),
            "importedRunways": len(imported_runways),
        },
        "missing": {
            "aerodromes": missing_aerodromes,
            "runways": missing_runways,
            "slzAreas": missing_slz,
        },
        "diff": {
            "previousAirac": previous.get("airac") if previous else None,
            "currentAirac": database["airac"],
            "changes": changes,
        },
        "blockedAutomaticOverwrites": blocked,
        "passed": not missing_aerodromes and not missing_runways and not missing_slz and not blocked,
    }
    return report


def import_runways(master_path: Path, output_path: Path, report_path: Path, previous_path: Path | None) -> int:
    master = require_dict(read_json(master_path), "$")
    previous = require_dict(read_json(previous_path), "previous") if previous_path else None
    try:
        database = normalized_database(master)
        report = build_coverage_report(master_path, master, database, previous)
        validate_against_schema(database, repository_root() / "schemas" / "aerodrome.schema.json", "aerodrome database")
        validate_against_schema(report, repository_root() / "Reports" / "airport_coverage.schema.json", "coverage report")
        if not report["passed"]:
            raise ImportErrorWithReport(report)
        write_json(output_path, database)
        write_json(report_path, report)
        return 0
    except ImportErrorWithReport as exc:
        write_json(report_path, exc.report)
        return 2
    except Exception as exc:
        report = {
            "schemaVersion": COVERAGE_SCHEMA_VERSION,
            "generatedAtUtc": now_utc(),
            "scope": master.get("metadata", {}).get("scope", "national_airport_master_list"),
            "airac": master.get("airac", {}),
            "approvedMasterList": {
                "sourcePath": str(master_path),
                "checksumSha256": sha256_file(master_path),
            },
            "counts": {
                "masterListActiveAerodromes": 0,
                "masterListActiveSlzAreas": 0,
                "importedAerodromes": 0,
                "importedRunways": 0,
            },
            "missing": {
                "aerodromes": [],
                "runways": [],
                "slzAreas": [],
            },
            "diff": {
                "previousAirac": previous.get("airac") if previous else None,
                "currentAirac": master.get("airac", {}),
                "changes": [],
            },
            "blockedAutomaticOverwrites": [
                {
                    "id": "import",
                    "changeType": "manual_geometry_overwrite_blocked",
                    "requiresManualReview": True,
                    "reason": str(exc),
                }
            ],
            "passed": False,
        }
        validate_against_schema(report, repository_root() / "Reports" / "airport_coverage.schema.json", "coverage report")
        write_json(report_path, report)
        return 2


def self_test() -> int:
    repo = Path(__file__).resolve().parents[2]
    seed = repo / "data_pipeline" / "seeds" / "pilot-airport-master-list.json"
    with tempfile.TemporaryDirectory(prefix="flying-import-runways-") as temp_name:
        temp = Path(temp_name)
        out = temp / "aerodromes.json"
        report = temp / "coverage.json"
        assert import_runways(seed, out, report, None) == 0
        output = read_json(out)
        coverage = read_json(report)
        assert output["schemaVersion"] == SCHEMA_VERSION
        assert coverage["passed"] is True
        assert coverage["missing"] == {"aerodromes": [], "runways": [], "slzAreas": []}
        assert all(
            end["threshold"]["physicalThreshold"]["positionWgs84"]
            for aerodrome in output["aerodromes"]
            for runway in aerodrome["runways"]
            for end in runway["ends"]
        )

        previous = copy.deepcopy(output)
        previous["aerodromes"][0]["runways"][0]["validation"]["status"] = "manually_verified"
        previous["aerodromes"][0]["runways"][0]["validation"]["manualVerification"]["status"] = "reviewer_approved"
        previous_path = temp / "previous.json"
        write_json(previous_path, previous)
        changed_seed = copy.deepcopy(read_json(seed))
        changed_seed["aerodromes"][0]["runways"][0]["dimensionsM"]["length"] += 10
        changed_path = temp / "changed-master.json"
        write_json(changed_path, changed_seed)
        assert import_runways(changed_path, temp / "changed.json", temp / "changed-report.json", previous_path) == 2
        assert read_json(temp / "changed-report.json")["blockedAutomaticOverwrites"]

        missing_seed = copy.deepcopy(read_json(seed))
        missing_seed["aerodromes"][0]["runways"].append(copy.deepcopy(missing_seed["aerodromes"][0]["runways"][0]))
        missing_seed["aerodromes"][0]["runways"][1]["id"] = "FPPV-RWY-10-28"
        missing_seed["aerodromes"][0]["runways"][1]["designator"] = "10/28"
        missing_path = temp / "missing-master.json"
        write_json(missing_path, missing_seed)
        missing_database = normalized_database(missing_seed)
        missing_database["aerodromes"][0]["runways"] = missing_database["aerodromes"][0]["runways"][:1]
        missing_report = build_coverage_report(missing_path, missing_seed, missing_database, None)
        validate_against_schema(missing_report, repo / "Reports" / "airport_coverage.schema.json", "coverage report")
        assert missing_report["passed"] is False
        assert missing_report["missing"]["runways"][0]["id"] == "FPPV/FPPV-RWY-10-28"
        assert missing_report["missing"]["runways"][0]["designator"] == "10/28"

        bad_derived_seed = copy.deepcopy(read_json(seed))
        bad_derived_threshold = bad_derived_seed["aerodromes"][0]["runways"][0]["ends"][0]["threshold"]
        bad_derived_threshold["geometrySource"] = "approved_derived_measurement"
        bad_derived_path = temp / "bad-derived-master.json"
        write_json(bad_derived_path, bad_derived_seed)
        assert import_runways(bad_derived_path, temp / "bad-derived.json", temp / "bad-derived-report.json", None) == 2
        assert "approvedDerivedMeasurement" in read_json(temp / "bad-derived-report.json")["blockedAutomaticOverwrites"][0]["reason"]

        bad_seed = copy.deepcopy(read_json(seed))
        del bad_seed["aerodromes"][0]["runways"][0]["ends"][0]["threshold"]["physicalThreshold"]["positionWgs84"]
        bad_path = temp / "bad-master.json"
        write_json(bad_path, bad_seed)
        assert import_runways(bad_path, temp / "bad.json", temp / "bad-report.json", None) == 2
        assert "positionWgs84" in read_json(temp / "bad-report.json")["blockedAutomaticOverwrites"][0]["reason"]
    return 0


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--master-list", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--coverage-report", type=Path)
    parser.add_argument("--previous", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    if not args.master_list or not args.output or not args.coverage_report:
        parser.error("--master-list, --output, and --coverage-report are required unless --self-test is used")
    return import_runways(args.master_list, args.output, args.coverage_report, args.previous)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
