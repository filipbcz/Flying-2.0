#!/usr/bin/env python3
"""Build and check the aircraft validation release-gate report."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from pathlib import Path


REQUIRED_CATEGORIES = {
    "stall_speeds",
    "maximum_and_cruise_speeds",
    "climb_rate",
    "service_ceiling",
    "glide",
    "sink_rate",
    "takeoff_distance",
    "landing_distance",
    "engine_rpm_power_fuel_temperature_points",
    "stability_modes",
    "control_step_responses",
    "coordinated_turns",
    "ground_effect",
    "crosswind",
    "tire_slip",
    "braking",
}

RESULT_STATUSES = {
    "pass",
    "fail",
    "approved_exclusion_missing_credible_data",
}

GRAPH_COLUMNS = ["metric", "reference", "actual", "deviation", "tolerance", "status"]
REPORT_SCHEMA = "flying.aircraft_validation_report.v1"
REPORT_PATH = Path("Reports/aircraft_validation/aircraft_validation_report.json")
SCHEMA_PATH = Path("Reports/aircraft_validation.schema.json")


def fail(message: str) -> None:
    raise ValueError(message)


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def stable_json(data: dict) -> str:
    return json.dumps(data, indent=2, sort_keys=True) + "\n"


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def type_matches(instance: object, expected: str) -> bool:
    if expected == "object":
        return isinstance(instance, dict)
    if expected == "array":
        return isinstance(instance, list)
    if expected == "string":
        return isinstance(instance, str)
    if expected == "boolean":
        return isinstance(instance, bool)
    if expected == "null":
        return instance is None
    return False


def schema_path(path: str, schema: dict) -> object:
    current: object = schema
    for part in path.removeprefix("#/").split("/"):
        require(isinstance(current, dict) and part in current, f"unknown schema ref: {path}")
        current = current[part]
    return current


def validate_json_schema(instance: object, schema: dict, root_schema: dict, location: str = "$") -> None:
    if "$ref" in schema:
        target = schema_path(schema["$ref"], root_schema)
        require(isinstance(target, dict), f"{location}: schema ref does not point to object")
        validate_json_schema(instance, target, root_schema, location)
        return

    for index, subschema in enumerate(schema.get("allOf", [])):
        validate_json_schema(instance, subschema, root_schema, f"{location}.allOf[{index}]")

    if "if" in schema:
        try:
            validate_json_schema(instance, schema["if"], root_schema, location)
            matched = True
        except ValueError:
            matched = False
        if matched and "then" in schema:
            validate_json_schema(instance, schema["then"], root_schema, location)
        if not matched and "else" in schema:
            validate_json_schema(instance, schema["else"], root_schema, location)

    if "const" in schema:
        require(instance == schema["const"], f"{location}: expected const {schema['const']!r}")
    if "enum" in schema:
        require(instance in schema["enum"], f"{location}: value {instance!r} not in enum")
    if "type" in schema:
        require(type_matches(instance, schema["type"]), f"{location}: expected {schema['type']}")

    if isinstance(instance, str):
        if "minLength" in schema:
            require(len(instance) >= schema["minLength"], f"{location}: string too short")
        if "maxLength" in schema:
            require(len(instance) <= schema["maxLength"], f"{location}: string too long")
        if "pattern" in schema:
            require(re.match(schema["pattern"], instance) is not None, f"{location}: pattern mismatch")

    if isinstance(instance, list):
        if "minItems" in schema:
            require(len(instance) >= schema["minItems"], f"{location}: too few items")
        if "maxItems" in schema:
            require(len(instance) <= schema["maxItems"], f"{location}: too many items")
        if schema.get("uniqueItems") is True:
            seen = set()
            for item in instance:
                key = json.dumps(item, sort_keys=True)
                require(key not in seen, f"{location}: duplicate item")
                seen.add(key)
        if "items" in schema:
            for index, item in enumerate(instance):
                validate_json_schema(item, schema["items"], root_schema, f"{location}[{index}]")

    if isinstance(instance, dict):
        for field in schema.get("required", []):
            require(field in instance, f"{location}: missing required field {field}")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            allowed = set(properties)
            unexpected = sorted(set(instance) - allowed)
            require(not unexpected, f"{location}: unexpected fields {', '.join(unexpected)}")
        for field, subschema in properties.items():
            if field in instance:
                validate_json_schema(instance[field], subschema, root_schema, f"{location}.{field}")


def read_blocker_ids(path: Path) -> set[str]:
    blocker_ids: set[str] = set()
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if line.startswith("- blocker_id:"):
            blocker_ids.add(line.split(":", 1)[1].strip().strip('"'))
    return blocker_ids


def validate_registry(registry: dict) -> dict[str, dict]:
    require(
        registry.get("schemaVersion") == "flying.aircraft_reference_registry.v1",
        "reference registry schema version mismatch",
    )
    require(registry.get("aircraftModel") == "flying_trainer_one", "unexpected aircraft model")
    require(
        registry.get("referenceAircraftIdentity", {}).get("faithfulTypeClaimPermitted") is False,
        "reference registry must prohibit faithful type claims while credible data is missing",
    )
    sources = {source["id"]: source for source in registry.get("sources", [])}
    require(sources, "reference registry must define at least one source")
    exclusion = sources.get("missing-poh-afm-flight-test-cfd-windtunnel")
    require(exclusion is not None, "missing credible reference data source is required")
    require(
        exclusion.get("approvedExclusion", {}).get("status") == "approved",
        "missing credible reference data must be an approved exclusion",
    )
    return sources


def validate_scenarios(scenarios_doc: dict, sources: dict[str, dict]) -> dict[str, dict]:
    require(
        scenarios_doc.get("schemaVersion") == "flying.aircraft_validation_scenarios.v1",
        "scenario schema version mismatch",
    )
    scenarios = scenarios_doc.get("scenarios", [])
    require(scenarios, "validation scenarios must not be empty")
    categories = {scenario.get("category") for scenario in scenarios}
    missing = sorted(REQUIRED_CATEGORIES - categories)
    require(not missing, f"missing validation categories: {', '.join(missing)}")

    by_id: dict[str, dict] = {}
    for scenario in scenarios:
        scenario_id = scenario.get("id")
        require(isinstance(scenario_id, str) and scenario_id, "each scenario must have an id")
        require(scenario_id not in by_id, f"duplicate scenario id: {scenario_id}")
        require(scenario.get("referenceSource") in sources, f"{scenario_id} references unknown source")
        require(isinstance(scenario.get("inputs"), dict), f"{scenario_id} must store inputs")
        require(scenario.get("outputs"), f"{scenario_id} must declare output metrics")
        require(isinstance(scenario.get("tolerance"), dict), f"{scenario_id} must store tolerance")

        source = sources[scenario["referenceSource"]]
        if source.get("credibleForTypeValidation") is True:
            require(
                scenario["tolerance"].get("status") != "not_defined_missing_credible_data",
                f"{scenario_id} has credible data and must define numeric tolerance",
            )
        else:
            require(
                scenario["tolerance"].get("status") == "not_defined_missing_credible_data",
                f"{scenario_id} must not invent tolerance without credible data",
            )
        by_id[scenario_id] = scenario
    return by_id


def validate_graph(root: Path, graph_rel: str, scenario: dict) -> list[dict]:
    graph_path = root / graph_rel
    require(graph_path.is_file(), f"missing deviation graph: {graph_path}")
    with graph_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.DictReader(handle)
        require(reader.fieldnames == GRAPH_COLUMNS, f"{graph_path} has wrong columns")
        rows = list(reader)
    require(rows, f"{graph_path} must contain graph rows")
    expected_metrics = set(scenario["outputs"])
    found_metrics: set[str] = set()
    graph_metrics: list[dict] = []
    for row in rows:
        metric = row["metric"]
        require(metric in expected_metrics, f"{graph_path} contains undeclared metric {metric}")
        require(metric not in found_metrics, f"{graph_path} contains duplicate metric {metric}")
        require(row["status"] in RESULT_STATUSES, f"{graph_path} contains invalid status")
        found_metrics.add(metric)
        if row["status"] == "approved_exclusion_missing_credible_data":
            row = {
                **row,
                "missingDataReason": (
                    "No licensed POH/AFM, flight-test, CFD, wind-tunnel, or equivalent "
                    "credible reference data is available for this mandatory validation metric."
                ),
            }
        else:
            for column in ("reference", "actual", "deviation", "tolerance"):
                require(row[column], f"{graph_path} {metric} missing {column}")
        graph_metrics.append(row)
    missing = sorted(expected_metrics - found_metrics)
    require(not missing, f"{graph_path} missing graph metrics: {', '.join(missing)}")
    return graph_metrics


def result_is_reference_backed(result: dict, scenario: dict, source: dict) -> bool:
    return (
        scenario.get("mandatoryWhenReferenceExists") is True
        and source.get("credibleForTypeValidation") is True
        and result.get("tolerance", {}).get("status") != "not_defined_missing_credible_data"
    )


def build_report(repo: Path) -> dict:
    root_rel = Path("docs/validation/aircraft/flying_trainer_one")
    root = repo / root_rel
    registry = load_json(root / "reference-registry.json")
    scenarios_doc = load_json(root / "validation-scenarios.json")
    results_doc = load_json(root / "validation-results.json")
    blocker_ids = read_blocker_ids(repo / "docs/blockers/external-inputs.yml")

    sources = validate_registry(registry)
    scenarios = validate_scenarios(scenarios_doc, sources)
    results = results_doc.get("results", [])
    require(results, "validation results must not be empty")
    require(
        {result.get("scenarioId") for result in results} == set(scenarios),
        "validation results must exactly cover scenario definitions",
    )
    require(results_doc.get("faithfulTypeClaim") is False, "faithful type claim must remain false")
    require(
        "aircraft-poh-afm-validation-rights" in blocker_ids,
        "external blocker aircraft-poh-afm-validation-rights must be recorded",
    )

    entries: list[dict] = []
    blocking_failures: list[str] = []
    exclusions: list[str] = []
    for result in results:
        scenario_id = result["scenarioId"]
        scenario = scenarios[scenario_id]
        source = sources[result["referenceSource"]]
        require(result.get("inputs") == scenario.get("inputs"), f"{scenario_id} inputs drifted")
        require(result.get("tolerance") == scenario.get("tolerance"), f"{scenario_id} tolerance drifted")
        require(set(result.get("outputs", {})) == set(scenario["outputs"]), f"{scenario_id} outputs drifted")
        require(result.get("resultStatus") in RESULT_STATUSES, f"{scenario_id} invalid status")

        reference_backed = result_is_reference_backed(result, scenario, source)
        require(result.get("releaseBlocking") is reference_backed, f"{scenario_id} releaseBlocking drifted")
        graph_metrics = validate_graph(root, result["deviationGraph"], scenario)
        if reference_backed and result["resultStatus"] == "fail":
            blocking_failures.append(scenario_id)
        blocked_by_missing_reference = False
        if result["resultStatus"] == "approved_exclusion_missing_credible_data":
            require(
                source.get("approvedExclusion", {}).get("status") == "approved",
                f"{scenario_id} exclusion lacks approval",
            )
            exclusions.append(scenario_id)
            blocked_by_missing_reference = True

        entries.append(
            {
                "scenarioId": scenario_id,
                "category": scenario["category"],
                "title": scenario["title"],
                "inputs": result["inputs"],
                "outputs": result["outputs"],
                "referenceSource": result["referenceSource"],
                "referenceSourceDocument": source["document"],
                "tolerance": result["tolerance"],
                "resultStatus": result["resultStatus"],
                "sourceReleaseBlocking": result["releaseBlocking"],
                "releaseBlocking": reference_backed or blocked_by_missing_reference,
                "stateHash": result["stateHash"],
                "deviationGraph": str(root_rel / result["deviationGraph"]),
                "deviationGraphRows": graph_metrics,
            }
        )

    require(not blocking_failures, f"mandatory reference-backed failures: {', '.join(blocking_failures)}")

    return {
        "schemaVersion": REPORT_SCHEMA,
        "aircraftModel": "flying_trainer_one",
        "suiteId": scenarios_doc["suiteId"],
        "runId": results_doc["runId"],
        "runStatus": results_doc["runStatus"],
        "sourceReleaseBlockingStatus": results_doc["releaseBlockingStatus"],
        "releaseBlockingStatus": (
            "blocked_missing_credible_reference_data"
            if exclusions
            else results_doc["releaseBlockingStatus"]
        ),
        "faithfulTypeClaim": False,
        "faithfulTypeClaimBlockedBy": "aircraft-poh-afm-validation-rights",
        "scenarioDefinitions": str(root_rel / "validation-scenarios.json"),
        "referenceRegistry": str(root_rel / "reference-registry.json"),
        "sourceResults": str(root_rel / "validation-results.json"),
        "blockerRegistry": "docs/blockers/external-inputs.yml",
        "requiredCategories": sorted(REQUIRED_CATEGORIES),
        "mandatoryReferenceBackedFailures": blocking_failures,
        "approvedExclusions": exclusions,
        "results": entries,
    }


def validate_report_shape(report: dict) -> None:
    require(report.get("schemaVersion") == REPORT_SCHEMA, "report schema version mismatch")
    require(report.get("faithfulTypeClaim") is False, "report must block faithful type claim")
    require(report.get("faithfulTypeClaimBlockedBy"), "report must name faithful-type blocker")
    require(not report.get("mandatoryReferenceBackedFailures"), "report contains mandatory failures")
    require(report.get("results"), "report must contain validation results")
    for entry in report["results"]:
        for field in (
            "inputs",
            "outputs",
            "referenceSource",
            "referenceSourceDocument",
            "tolerance",
            "deviationGraph",
            "deviationGraphRows",
            "resultStatus",
        ):
            require(field in entry, f"{entry.get('scenarioId', '<unknown>')} missing {field}")


def validate_schema_file(repo: Path) -> None:
    schema = load_json(repo / SCHEMA_PATH)
    require(schema.get("$id") == REPORT_SCHEMA, "aircraft validation schema id mismatch")
    require(
        schema.get("properties", {}).get("schemaVersion", {}).get("const") == REPORT_SCHEMA,
        "aircraft validation schemaVersion const mismatch",
    )
    require(
        schema.get("properties", {}).get("faithfulTypeClaim", {}).get("const") is False,
        "aircraft validation schema must prohibit faithful type claims while blockers remain",
    )


def validate_report_against_schema(repo: Path, report: dict) -> None:
    schema = load_json(repo / SCHEMA_PATH)
    validate_json_schema(report, schema, schema)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--write", action="store_true", help="write Reports/aircraft_validation")
    parser.add_argument("--check", action="store_true", help="verify checked-in report is current")
    args = parser.parse_args()

    repo = args.repo.resolve()
    report = build_report(repo)
    validate_report_shape(report)
    validate_schema_file(repo)
    validate_report_against_schema(repo, report)
    output = stable_json(report)
    report_path = repo / REPORT_PATH

    if args.write:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(output, encoding="utf-8")

    if args.check:
        require(report_path.is_file(), f"missing report: {REPORT_PATH}")
        require(report_path.read_text(encoding="utf-8") == output, f"{REPORT_PATH} is stale")

    if not args.write and not args.check:
        print(output, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ValueError as error:
        print(str(error), file=sys.stderr)
        raise SystemExit(1)
