#!/usr/bin/env python3
"""Focused contract and evidence gate validation for Flying 2.0."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = REPO_ROOT / "docs" / "contract" / "flying-2.0.yml"
LEDGER_SCHEMA_PATH = REPO_ROOT / "docs" / "evidence" / "ledger.schema.json"
REQ_ID = re.compile(r"^REQ-[A-Z0-9]+(?:-[A-Z0-9]+)*$")
MILESTONE_ID = re.compile(r"^M[0-9]+$")


class ValidationError(Exception):
    pass


def load_json(path: Path) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValidationError(f"missing required file: {path.relative_to(REPO_ROOT)}") from exc
    except json.JSONDecodeError as exc:
        raise ValidationError(f"invalid JSON/YAML subset in {path.relative_to(REPO_ROOT)}: {exc}") from exc


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValidationError(message)


def validate_contract() -> dict[str, Any]:
    contract = load_json(CONTRACT_PATH)
    schema = load_json(LEDGER_SCHEMA_PATH)

    require(contract.get("schemaVersion") == "flying.contract.v1", "contract schemaVersion mismatch")
    require(contract.get("contractId") == "flying-2.0", "contractId mismatch")
    require(contract.get("status") == "immutable", "contract must be immutable")

    requirements = contract.get("requirements")
    milestones = contract.get("milestones")
    require(isinstance(requirements, list) and requirements, "contract requirements must be a non-empty list")
    require(isinstance(milestones, list) and milestones, "contract milestones must be a non-empty list")

    milestone_ids = set()
    for milestone in milestones:
        milestone_id = milestone.get("id")
        require(isinstance(milestone_id, str) and MILESTONE_ID.match(milestone_id) is not None,
                f"invalid milestone id: {milestone_id}")
        require(milestone_id not in milestone_ids, f"duplicate milestone id: {milestone_id}")
        milestone_ids.add(milestone_id)

    requirement_ids = set()
    for requirement in requirements:
        requirement_id = requirement.get("id")
        require(isinstance(requirement_id, str) and REQ_ID.match(requirement_id) is not None,
                f"invalid requirement id: {requirement_id}")
        require(requirement_id not in requirement_ids, f"duplicate requirement id: {requirement_id}")
        requirement_ids.add(requirement_id)
        require(requirement.get("mandatory") is True, f"requirement must be mandatory: {requirement_id}")
        require(requirement.get("milestone") in milestone_ids,
                f"requirement {requirement_id} maps to unknown milestone")
        require(bool(requirement.get("evidenceGate")), f"requirement missing evidence gate: {requirement_id}")

    mapped_requirement_ids = set()
    for milestone in milestones:
        for requirement_id in milestone.get("requiredRequirementIds", []):
            require(requirement_id in requirement_ids,
                    f"milestone {milestone.get('id')} maps unknown requirement: {requirement_id}")
            mapped_requirement_ids.add(requirement_id)

    missing_mappings = requirement_ids - mapped_requirement_ids
    require(not missing_mappings, f"requirements missing milestone mapping: {sorted(missing_mappings)}")

    ledger = contract.get("evidenceLedger", {})
    require(ledger.get("schema") == "docs/evidence/ledger.schema.json", "contract must link evidence ledger schema")
    require(ledger.get("missingMandatoryEvidenceBehavior") == "block_release",
            "missing mandatory evidence must block release")

    validate_ledger_schema(schema)
    return contract


def validate_ledger_schema(schema: dict[str, Any]) -> None:
    require(schema.get("$schema") == "https://json-schema.org/draft/2020-12/schema",
            "ledger schema must use JSON Schema draft 2020-12")
    require(schema.get("properties", {}).get("schemaVersion", {}).get("const") == "flying.evidence-ledger.v1",
            "ledger schemaVersion const mismatch")
    requirement_schema = schema.get("$defs", {}).get("requirementEvidence", {})
    require(requirement_schema.get("properties", {}).get("requirementId", {}).get("pattern") == REQ_ID.pattern,
            "ledger requirement id pattern mismatch")
    any_of = requirement_schema.get("anyOf")
    require(isinstance(any_of, list), "ledger requirement evidence must declare anyOf proof/blocker rule")
    required_sets = {tuple(item.get("required", [])) for item in any_of if isinstance(item, dict)}
    require(("proofRefs",) in required_sets and ("blockerRef",) in required_sets,
            "ledger must reject requirements without proofRefs or blockerRef")
    proof_refs = requirement_schema.get("properties", {}).get("proofRefs", {})
    require(proof_refs.get("minItems") == 1, "ledger proofRefs must require at least one proof")


def validate_ledger_entries(contract: dict[str, Any], ledger: dict[str, Any]) -> None:
    requirement_by_id = {item["id"]: item for item in contract["requirements"]}
    entries = ledger.get("requirements")
    require(isinstance(entries, list) and entries, "evidence ledger requirements must be a non-empty list")

    seen = set()
    for entry in entries:
        requirement_id = entry.get("requirementId")
        require(requirement_id in requirement_by_id, f"ledger references unknown requirement: {requirement_id}")
        require(requirement_id not in seen, f"duplicate ledger evidence entry: {requirement_id}")
        seen.add(requirement_id)
        has_proof = bool(entry.get("proofRefs"))
        has_blocker = bool(entry.get("blockerRef"))
        require(has_proof or has_blocker,
                f"ledger entry must link proofRefs or blockerRef: {requirement_id}")
        require(entry.get("milestone") == requirement_by_id[requirement_id]["milestone"],
                f"ledger milestone mismatch: {requirement_id}")
        if requirement_by_id[requirement_id].get("mandatory") is True:
            require(entry.get("mandatory") is True, f"mandatory flag mismatch: {requirement_id}")

    missing = set(requirement_by_id) - seen
    require(not missing, f"evidence ledger missing requirements: {sorted(missing)}")


def command_contract(_: argparse.Namespace) -> int:
    validate_contract()
    print("forge_validate: contract ok")
    return 0


def command_architecture(_: argparse.Namespace) -> int:
    validate_contract()
    print("forge_validate: architecture ok")
    return 0


def command_release_gate(args: argparse.Namespace) -> int:
    contract = validate_contract()
    ledger = load_json(REPO_ROOT / args.ledger)
    validate_ledger_entries(contract, ledger)

    failures = [
        entry["requirementId"]
        for entry in ledger["requirements"]
        if entry.get("mandatory") is True and entry.get("status") != "pass"
    ]
    if failures:
        raise ValidationError(f"mandatory evidence is not passing: {failures}")

    print("forge_validate: release gate ok")
    return 0


def command_missing_evidence_selftest(_: argparse.Namespace) -> int:
    contract = validate_contract()
    bad_ledger = {
        "schemaVersion": "flying.evidence-ledger.v1",
        "contractId": contract["contractId"],
        "generatedAtUtc": "2026-08-11T00:00:00Z",
        "requirements": [
            {
                "requirementId": requirement["id"],
                "milestone": requirement["milestone"],
                "mandatory": requirement["mandatory"],
                "status": "pending",
            }
            for requirement in contract["requirements"]
        ],
    }

    try:
        validate_ledger_entries(contract, bad_ledger)
    except ValidationError:
        print("forge_validate: missing mandatory evidence rejected")
        return 0

    raise ValidationError("missing mandatory evidence was accepted")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Flying 2.0 contract evidence gates.")
    subcommands = parser.add_subparsers(dest="command", required=True)

    contract = subcommands.add_parser("contract", help="validate contract registry and ledger schema")
    contract.set_defaults(func=command_contract)

    architecture = subcommands.add_parser("architecture", help="validate architecture-facing contract gates")
    architecture.set_defaults(func=command_architecture)

    architectur = subcommands.add_parser("architectur", help="compatibility alias for architecture")
    architectur.set_defaults(func=command_architecture)

    release_gate = subcommands.add_parser("release-gate", help="validate an evidence ledger for release")
    release_gate.add_argument("--ledger", default="docs/evidence/ledger.json")
    release_gate.set_defaults(func=command_release_gate)

    missing_evidence = subcommands.add_parser(
        "missing-evidence-selftest",
        help="prove mandatory requirements without proof or blockers are rejected",
    )
    missing_evidence.set_defaults(func=command_missing_evidence_selftest)

    args = parser.parse_args()
    try:
        return args.func(args)
    except ValidationError as exc:
        print(f"forge_validate: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
