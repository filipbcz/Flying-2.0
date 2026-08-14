#!/usr/bin/env python3
"""Validate Flying offline GIS source manifests.

The validator is deliberately dependency-free so source intake gates can run on
clean build machines before Python package installation is available.
"""

from __future__ import annotations

import argparse
import copy
import datetime as dt
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Any


REQUIRED_GROUPS = (
    "dmr",
    "dmp",
    "ortofoto",
    "zabaged",
    "geonames",
    "ucl",
    "aip",
    "vfr",
    "operator_inputs",
)

ALLOWED_CHECKSUMS = {"sha256"}
SHA256_RE = re.compile(r"^[A-Fa-f0-9]{64}$")
DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


class ValidationFinding:
    def __init__(self, severity: str, code: str, path: str, message: str) -> None:
        self.severity = severity
        self.code = code
        self.path = path
        self.message = message

    def to_report_item(self) -> dict[str, str]:
        return {
            "severity": self.severity,
            "code": self.code,
            "path": self.path,
            "message": self.message,
        }


def is_non_empty_string(value: Any) -> bool:
    return isinstance(value, str) and value.strip() != ""


def parse_json_file(path: Path) -> Any:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except FileNotFoundError as exc:
        raise ValueError(f"Manifest does not exist: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"Manifest is not valid JSON: {exc}") from exc


def require_object(value: Any, path: str, findings: list[ValidationFinding]) -> dict[str, Any] | None:
    if not isinstance(value, dict):
        findings.append(ValidationFinding("error", "type.object", path, "Expected an object."))
        return None
    return value


def require_array(value: Any, path: str, findings: list[ValidationFinding]) -> list[Any] | None:
    if not isinstance(value, list):
        findings.append(ValidationFinding("error", "type.array", path, "Expected an array."))
        return None
    return value


def require_field(
    obj: dict[str, Any],
    field: str,
    path: str,
    findings: list[ValidationFinding],
) -> Any:
    if field not in obj:
        findings.append(
            ValidationFinding(
                "error",
                "field.missing",
                f"{path}.{field}",
                f"Missing required field '{field}'.",
            )
        )
        return None
    return obj[field]


def require_non_empty(
    obj: dict[str, Any],
    field: str,
    path: str,
    findings: list[ValidationFinding],
) -> str | None:
    value = require_field(obj, field, path, findings)
    if value is None:
        return None
    if not is_non_empty_string(value):
        findings.append(
            ValidationFinding(
                "error",
                "field.empty",
                f"{path}.{field}",
                f"Field '{field}' must be a non-empty string.",
            )
        )
        return None
    return value.strip()


def validate_iso_date(value: Any, path: str, findings: list[ValidationFinding]) -> None:
    if not is_non_empty_string(value) or not DATE_RE.match(value):
        findings.append(
            ValidationFinding(
                "error",
                "date.invalid",
                path,
                "Expected an ISO 8601 calendar date in YYYY-MM-DD form.",
            )
        )
        return
    try:
        dt.date.fromisoformat(value)
    except ValueError:
        findings.append(
            ValidationFinding("error", "date.invalid", path, "Date is not a valid calendar day.")
        )


def validate_crs(value: Any, path: str, findings: list[ValidationFinding]) -> None:
    crs = require_object(value, path, findings)
    if crs is None:
        return
    require_non_empty(crs, "authority", path, findings)
    require_non_empty(crs, "code", path, findings)
    require_non_empty(crs, "name", path, findings)
    if "verticalDatum" in crs and not is_non_empty_string(crs["verticalDatum"]):
        findings.append(
            ValidationFinding(
                "error",
                "crs.vertical_datum.invalid",
                f"{path}.verticalDatum",
                "CRS verticalDatum must be a non-empty string when supplied.",
            )
        )


def validate_transform(value: Any, path: str, findings: list[ValidationFinding]) -> None:
    transform = require_object(value, path, findings)
    if transform is None:
        return
    validate_crs(require_field(transform, "sourceCrs", path, findings), f"{path}.sourceCrs", findings)
    validate_crs(require_field(transform, "targetCrs", path, findings), f"{path}.targetCrs", findings)
    pipeline = require_non_empty(transform, "projPipeline", path, findings)
    grid_files = require_array(require_field(transform, "gridFiles", path, findings), f"{path}.gridFiles", findings)
    if grid_files is not None and len(grid_files) == 0:
        findings.append(
            ValidationFinding(
                "error",
                "transform.grid_files.empty",
                f"{path}.gridFiles",
                "At least one PROJ grid or explicit 'none' sentinel must be declared.",
            )
        )
    if pipeline and "+proj" not in pipeline:
        findings.append(
            ValidationFinding(
                "error",
                "transform.proj_pipeline.invalid",
                f"{path}.projPipeline",
                "PROJ pipeline must include a +proj step declaration.",
            )
        )


def validate_checksum(
    value: Any,
    path: str,
    base_dir: Path,
    verify_files: bool,
    findings: list[ValidationFinding],
) -> None:
    checksum = require_object(value, path, findings)
    if checksum is None:
        return
    algorithm = require_non_empty(checksum, "algorithm", path, findings)
    digest = require_non_empty(checksum, "value", path, findings)
    payload_path = require_non_empty(checksum, "path", path, findings)
    normalized_algorithm = algorithm.lower() if algorithm else None
    if normalized_algorithm and normalized_algorithm not in ALLOWED_CHECKSUMS:
        findings.append(
            ValidationFinding(
                "error",
                "checksum.algorithm.unsupported",
                f"{path}.algorithm",
                "Only sha256 checksums are accepted for source imports.",
            )
        )
    if digest and not SHA256_RE.match(digest):
        findings.append(
            ValidationFinding(
                "error",
                "checksum.value.invalid",
                f"{path}.value",
                "SHA-256 checksum value must be 64 hexadecimal characters.",
            )
        )
    if not verify_files or not payload_path or not digest or normalized_algorithm != "sha256":
        return
    candidate = Path(payload_path)
    if not candidate.is_absolute():
        candidate = base_dir / candidate
    if not candidate.is_file():
        findings.append(
            ValidationFinding(
                "error",
                "checksum.path.missing",
                f"{path}.path",
                f"Checksum payload path does not exist: {candidate}",
            )
        )
        return
    sha = hashlib.sha256()
    with candidate.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            sha.update(chunk)
    actual = sha.hexdigest()
    if actual.lower() != digest.lower():
        findings.append(
            ValidationFinding(
                "error",
                "checksum.mismatch",
                f"{path}.value",
                f"Checksum mismatch for {candidate}: expected {digest.lower()}, got {actual}.",
            )
        )


def validate_license(value: Any, path: str, findings: list[ValidationFinding]) -> None:
    license_obj = require_object(value, path, findings)
    if license_obj is None:
        return
    require_non_empty(license_obj, "name", path, findings)
    require_non_empty(license_obj, "spdxId", path, findings)
    require_non_empty(license_obj, "redistributionTerms", path, findings)
    require_non_empty(license_obj, "evidenceUri", path, findings)


def validate_attribution(value: Any, path: str, findings: list[ValidationFinding]) -> None:
    attribution = require_object(value, path, findings)
    if attribution is None:
        return
    require_non_empty(attribution, "statement", path, findings)
    require_non_empty(attribution, "requiredInProduct", path, findings)


def validate_provenance(value: Any, path: str, findings: list[ValidationFinding]) -> None:
    provenance = require_object(value, path, findings)
    if provenance is None:
        return
    require_non_empty(provenance, "publisher", path, findings)
    require_non_empty(provenance, "sourceUri", path, findings)
    require_non_empty(provenance, "retrievedAtUtc", path, findings)
    require_non_empty(provenance, "packageId", path, findings)
    effective_date = require_field(provenance, "effectiveDate", path, findings)
    if effective_date is not None:
        validate_iso_date(effective_date, f"{path}.effectiveDate", findings)


def validate_source(
    value: Any,
    path: str,
    base_dir: Path,
    verify_files: bool,
    findings: list[ValidationFinding],
) -> None:
    source = require_object(value, path, findings)
    if source is None:
        return
    require_non_empty(source, "id", path, findings)
    require_non_empty(source, "name", path, findings)
    require_non_empty(source, "version", path, findings)
    validate_checksum(require_field(source, "checksum", path, findings), f"{path}.checksum", base_dir, verify_files, findings)
    validate_crs(require_field(source, "crs", path, findings), f"{path}.crs", findings)
    validate_transform(require_field(source, "transform", path, findings), f"{path}.transform", findings)
    validate_license(require_field(source, "license", path, findings), f"{path}.license", findings)
    validate_attribution(require_field(source, "attribution", path, findings), f"{path}.attribution", findings)
    validate_provenance(require_field(source, "provenance", path, findings), f"{path}.provenance", findings)


def validate_manifest(manifest: Any, base_dir: Path, verify_files: bool = True) -> list[ValidationFinding]:
    findings: list[ValidationFinding] = []
    root = require_object(manifest, "$", findings)
    if root is None:
        return findings
    schema_version = require_non_empty(root, "schemaVersion", "$", findings)
    if schema_version and schema_version != "flying.source-manifest.v1":
        findings.append(
            ValidationFinding(
                "error",
                "schema.version.unsupported",
                "$.schemaVersion",
                "Expected schemaVersion 'flying.source-manifest.v1'.",
            )
        )
    require_non_empty(root, "manifestVersion", "$", findings)
    source_groups = require_object(require_field(root, "sourceGroups", "$", findings), "$.sourceGroups", findings)
    if source_groups is None:
        return findings
    for group_name in REQUIRED_GROUPS:
        group_path = f"$.sourceGroups.{group_name}"
        group = require_array(require_field(source_groups, group_name, "$.sourceGroups", findings), group_path, findings)
        if group is None:
            continue
        if len(group) == 0:
            findings.append(
                ValidationFinding(
                    "error",
                    "source_group.empty",
                    group_path,
                    f"Source group '{group_name}' must contain at least one source package.",
                )
            )
        for index, source in enumerate(group):
            validate_source(source, f"{group_path}[{index}]", base_dir, verify_files, findings)
    for group_name in sorted(set(source_groups) - set(REQUIRED_GROUPS)):
        findings.append(
            ValidationFinding(
                "warning",
                "source_group.unknown",
                f"$.sourceGroups.{group_name}",
                "Unknown source group is ignored by the acceptance gate.",
            )
        )
    return findings


def build_report(manifest_path: Path, findings: list[ValidationFinding]) -> dict[str, Any]:
    error_count = sum(1 for finding in findings if finding.severity == "error")
    warning_count = sum(1 for finding in findings if finding.severity == "warning")
    return {
        "schemaVersion": "flying.source-validation-report.v1",
        "manifestPath": str(manifest_path),
        "generatedAtUtc": dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z"),
        "status": "pass" if error_count == 0 else "fail",
        "summary": {
            "requiredSourceGroups": list(REQUIRED_GROUPS),
            "errorCount": error_count,
            "warningCount": warning_count,
        },
        "findings": [finding.to_report_item() for finding in findings],
    }


def write_report(report: dict[str, Any], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(report, handle, indent=2, sort_keys=True)
        handle.write("\n")


def sample_source(group: str, payload_name: str, digest: str) -> dict[str, Any]:
    return {
        "id": f"{group}-sample",
        "name": f"{group.upper()} sample package",
        "version": "2026.08",
        "checksum": {
            "algorithm": "sha256",
            "path": payload_name,
            "value": digest,
        },
        "crs": {
            "authority": "EPSG",
            "code": "5514",
            "name": "S-JTSK / Krovak East North",
            "verticalDatum": "Baltic 1957 height",
        },
        "transform": {
            "sourceCrs": {
                "authority": "EPSG",
                "code": "5514",
                "name": "S-JTSK / Krovak East North",
            },
            "targetCrs": {
                "authority": "EPSG",
                "code": "4979",
                "name": "WGS 84 3D",
            },
            "projPipeline": "+proj=pipeline +step +proj=axisswap +order=2,1",
            "gridFiles": ["cz-geoid-model.gtx"],
        },
        "license": {
            "name": "Test license",
            "spdxId": "LicenseRef-Flying-Test",
            "redistributionTerms": "Internal validation fixture only.",
            "evidenceUri": "docs/evidence/m0-legal-data-gate.yml",
        },
        "attribution": {
            "statement": "Test attribution",
            "requiredInProduct": "yes",
        },
        "provenance": {
            "publisher": "Flying test fixture",
            "sourceUri": "fixture://source",
            "retrievedAtUtc": "2026-08-14T00:00:00Z",
            "packageId": f"{group}-fixture-package",
            "effectiveDate": "2026-08-14",
        },
    }


def build_sample_manifest(payload_name: str, digest: str) -> dict[str, Any]:
    return {
        "schemaVersion": "flying.source-manifest.v1",
        "manifestVersion": "2026.08.acceptance-fixture",
        "sourceGroups": {
            group: [sample_source(group, payload_name, digest)] for group in REQUIRED_GROUPS
        },
    }


def assert_self_test_case(
    name: str,
    manifest: dict[str, Any],
    base_dir: Path,
    expect_pass: bool,
    required_code: str | None = None,
) -> None:
    findings = validate_manifest(manifest, base_dir, verify_files=True)
    errors = [finding for finding in findings if finding.severity == "error"]
    passed = len(errors) == 0
    if passed != expect_pass:
        details = ", ".join(f"{finding.code}@{finding.path}" for finding in findings)
        raise AssertionError(f"{name}: expected pass={expect_pass}, got pass={passed}; findings: {details}")
    if required_code and not any(finding.code == required_code for finding in findings):
        details = ", ".join(f"{finding.code}@{finding.path}" for finding in findings)
        raise AssertionError(f"{name}: expected finding code {required_code}; findings: {details}")


def assert_report_consistency(name: str, report: dict[str, Any]) -> None:
    error_count = report["summary"]["errorCount"]
    expected_status = "pass" if error_count == 0 else "fail"
    if report["status"] != expected_status:
        raise AssertionError(
            f"{name}: expected report status {expected_status} for {error_count} error(s), got {report['status']}"
        )
    try:
        dt.datetime.fromisoformat(report["generatedAtUtc"].replace("Z", "+00:00"))
    except ValueError as exc:
        raise AssertionError(f"{name}: generatedAtUtc is not a valid date-time") from exc


def run_self_test() -> int:
    with tempfile.TemporaryDirectory(prefix="flying-source-validation-") as temp_name:
        temp_dir = Path(temp_name)
        payload = temp_dir / "source.bin"
        payload.write_bytes(b"flying source validation fixture\n")
        digest = hashlib.sha256(payload.read_bytes()).hexdigest()
        valid_manifest = build_sample_manifest(payload.name, digest)
        assert_self_test_case("valid manifest", valid_manifest, temp_dir, True)

        missing_checksum = copy.deepcopy(valid_manifest)
        del missing_checksum["sourceGroups"]["dmr"][0]["checksum"]
        assert_self_test_case("missing checksum", missing_checksum, temp_dir, False, "field.missing")

        missing_crs = copy.deepcopy(valid_manifest)
        del missing_crs["sourceGroups"]["dmp"][0]["crs"]
        assert_self_test_case("missing crs", missing_crs, temp_dir, False, "field.missing")

        missing_license = copy.deepcopy(valid_manifest)
        del missing_license["sourceGroups"]["ortofoto"][0]["license"]
        assert_self_test_case("missing license", missing_license, temp_dir, False, "field.missing")

        missing_attribution = copy.deepcopy(valid_manifest)
        del missing_attribution["sourceGroups"]["zabaged"][0]["attribution"]
        assert_self_test_case("missing attribution", missing_attribution, temp_dir, False, "field.missing")

        missing_effective_date = copy.deepcopy(valid_manifest)
        del missing_effective_date["sourceGroups"]["geonames"][0]["provenance"]["effectiveDate"]
        assert_self_test_case("missing effective date", missing_effective_date, temp_dir, False, "field.missing")

        missing_group = copy.deepcopy(valid_manifest)
        del missing_group["sourceGroups"]["ucl"]
        assert_self_test_case("missing separated UCL group", missing_group, temp_dir, False, "field.missing")

        mismatched_checksum = copy.deepcopy(valid_manifest)
        mismatched_checksum["sourceGroups"]["aip"][0]["checksum"]["value"] = "0" * 64
        assert_self_test_case("checksum mismatch", mismatched_checksum, temp_dir, False, "checksum.mismatch")

        uppercase_missing = copy.deepcopy(valid_manifest)
        uppercase_missing["sourceGroups"]["vfr"][0]["checksum"]["algorithm"] = "SHA256"
        uppercase_missing["sourceGroups"]["vfr"][0]["checksum"]["path"] = "missing.bin"
        assert_self_test_case(
            "uppercase checksum algorithm still verifies path",
            uppercase_missing,
            temp_dir,
            False,
            "checksum.path.missing",
        )

        metadata_only = copy.deepcopy(valid_manifest)
        metadata_only["sourceGroups"]["operator_inputs"][0]["checksum"]["path"] = "missing-in-metadata-only.bin"
        metadata_only_findings = validate_manifest(metadata_only, temp_dir, verify_files=False)
        if any(finding.code == "checksum.path.missing" for finding in metadata_only_findings):
            raise AssertionError("metadata-only validation must not read checksum payload files")
        if any(finding.severity == "error" for finding in metadata_only_findings):
            details = ", ".join(f"{finding.code}@{finding.path}" for finding in metadata_only_findings)
            raise AssertionError(f"metadata-only validation should pass complete declarations; findings: {details}")

        unknown_group = copy.deepcopy(valid_manifest)
        unknown_group["sourceGroups"]["experimental"] = [sample_source("experimental", payload.name, digest)]
        unknown_group_findings = validate_manifest(unknown_group, temp_dir, verify_files=True)
        if not any(finding.code == "source_group.unknown" for finding in unknown_group_findings):
            raise AssertionError("unknown source group should produce a warning")
        if any(finding.severity == "error" for finding in unknown_group_findings):
            details = ", ".join(f"{finding.code}@{finding.path}" for finding in unknown_group_findings)
            raise AssertionError(f"unknown source group should not fail required groups; findings: {details}")

        pass_report = build_report(temp_dir / "valid.json", validate_manifest(valid_manifest, temp_dir))
        assert_report_consistency("pass report", pass_report)
        fail_report = build_report(
            temp_dir / "malformed.json",
            [ValidationFinding("error", "manifest.unreadable", "$", "Manifest is not valid JSON.")],
        )
        assert_report_consistency("malformed report", fail_report)
    print("source manifest validation self-test passed")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, help="Source manifest JSON to validate.")
    parser.add_argument("--report", type=Path, help="Machine-readable validation report JSON to write.")
    parser.add_argument(
        "--base-dir",
        type=Path,
        help="Base directory for relative checksum paths. Defaults to the manifest directory.",
    )
    parser.add_argument(
        "--metadata-only",
        action="store_true",
        help="Validate checksum declarations without reading payload files.",
    )
    parser.add_argument("--self-test", action="store_true", help="Run focused acceptance self-tests.")
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()
    if args.manifest is None or args.report is None:
        parser.error("--manifest and --report are required unless --self-test is used")

    try:
        manifest = parse_json_file(args.manifest)
    except ValueError as exc:
        report = build_report(args.manifest, [ValidationFinding("error", "manifest.unreadable", "$", str(exc))])
        write_report(report, args.report)
        print(str(exc), file=sys.stderr)
        return 1

    base_dir = args.base_dir if args.base_dir else args.manifest.resolve().parent
    findings = validate_manifest(manifest, base_dir, verify_files=not args.metadata_only)
    report = build_report(args.manifest, findings)
    write_report(report, args.report)
    if report["status"] != "pass":
        print(f"source validation failed with {report['summary']['errorCount']} error(s)", file=sys.stderr)
        return 1
    print(f"source validation passed: {args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
