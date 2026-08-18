#!/usr/bin/env python3
"""Focused contract and evidence gate validation for Flying 2.0."""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Any


REPO_ROOT = Path(__file__).resolve().parents[1]
CONTRACT_PATH = REPO_ROOT / "docs" / "contract" / "flying-2.0.yml"
LEDGER_SCHEMA_PATH = REPO_ROOT / "docs" / "evidence" / "ledger.schema.json"
REQ_ID = re.compile(r"^REQ-[A-Z0-9]+(?:-[A-Z0-9]+)*$")
MILESTONE_ID = re.compile(r"^M[0-9]+$")
INCLUDE_RE = re.compile(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]', re.MULTILINE)

MODULE_BOUNDARIES = {
    "CoreSim": {
        "paths": [Path("core_sim"), Path("Source/CoreSim")],
        "forbidden_include_prefixes": (
            "CoreMinimal.h",
            "Blueprint/",
            "Cesium",
            "FlyingPresentation",
            "unreal/",
        ),
        "forbidden_link_tokens": (
            "FlyingPresentation",
            "Cesium",
            "Unreal",
            "Blueprint",
        ),
    },
    "GeoTerrain": {
        "paths": [Path("geo_terrain"), Path("Source/GeoTerrain")],
        "forbidden_include_prefixes": (
            "FlyingPresentation/Private",
            "unreal/Source/FlyingPresentation/Private",
        ),
        "forbidden_link_tokens": (
            "FlyingPresentationPrivate",
        ),
    },
}


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
    one_of = requirement_schema.get("oneOf")
    evidence_options = one_of or requirement_schema.get("anyOf")
    require(isinstance(evidence_options, list), "ledger requirement evidence must declare proof/blocker rule")
    required_sets = {tuple(item.get("required", [])) for item in evidence_options if isinstance(item, dict)}
    require(("proofRefs",) in required_sets and ("blockerRef",) in required_sets,
            "ledger must reject requirements without proofRefs or blockerRef")
    require(isinstance(one_of, list), "ledger must make proofRefs and blockerRef mutually exclusive")
    proof_refs = requirement_schema.get("properties", {}).get("proofRefs", {})
    require(proof_refs.get("minItems") == 1, "ledger proofRefs must require at least one proof")


def ref_base(ref: str) -> str:
    return ref.split("#", 1)[0]


def validate_proof_ref(entry: dict[str, Any], proof_ref: dict[str, Any]) -> None:
    requirement_id = entry.get("requirementId")
    require(isinstance(proof_ref, dict), f"invalid proofRef: {requirement_id}")
    require(proof_ref.get("status") == "pass", f"proofRef must be passing: {requirement_id}")
    require(proof_ref.get("kind") != "external", f"external references are blockers, not proof: {requirement_id}")
    path_text = proof_ref.get("path")
    require(isinstance(path_text, str) and path_text, f"proofRef path is required: {requirement_id}")
    require(
        re.search(r"(^|[/\\])(fixtures?|synthetic|declarations?)([/\\]|$)", path_text, flags=re.IGNORECASE) is None,
        f"synthetic, fixture, or declaration path cannot be production proof: {requirement_id}",
    )
    if proof_ref.get("kind") != "command":
        proof_path = (REPO_ROOT / ref_base(path_text)).resolve()
        require(
            proof_path == REPO_ROOT or REPO_ROOT in proof_path.parents,
            f"proofRef path must stay inside the repository: {requirement_id}",
        )
        require(proof_path.exists(), f"proofRef path is missing: {path_text}")


def validate_blocker_ref(entry: dict[str, Any]) -> None:
    blocker_ref = entry.get("blockerRef")
    requirement_id = entry.get("requirementId")
    prefix = "docs/blockers/external-inputs.yml#"
    require(isinstance(blocker_ref, str) and blocker_ref.startswith(prefix),
            f"blockerRef must link the external blocker registry: {requirement_id}")
    blocker_id = blocker_ref[len(prefix):]
    registry = (REPO_ROOT / "docs" / "blockers" / "external-inputs.yml").read_text(encoding="utf-8")
    require(f"blocker_id: {blocker_id}" in registry,
            f"blockerRef does not exist in external blocker registry: {blocker_ref}")


def validate_ledger_entries(contract: dict[str, Any], ledger: dict[str, Any]) -> None:
    require(ledger.get("schemaVersion") == "flying.evidence-ledger.v1", "ledger schemaVersion mismatch")
    require(ledger.get("contractId") == contract.get("contractId"), "ledger contractId mismatch")
    requirement_by_id = {item["id"]: item for item in contract["requirements"]}
    entries = ledger.get("requirements")
    require(isinstance(entries, list) and entries, "evidence ledger requirements must be a non-empty list")

    seen = set()
    for entry in entries:
        requirement_id = entry.get("requirementId")
        require(requirement_id in requirement_by_id, f"ledger references unknown requirement: {requirement_id}")
        require(requirement_id not in seen, f"duplicate ledger evidence entry: {requirement_id}")
        seen.add(requirement_id)
        has_proof = isinstance(entry.get("proofRefs"), list) and bool(entry.get("proofRefs"))
        has_blocker = bool(entry.get("blockerRef"))
        require(has_proof != has_blocker,
                f"ledger entry must link exactly one of proofRefs or blockerRef: {requirement_id}")
        require(entry.get("milestone") == requirement_by_id[requirement_id]["milestone"],
                f"ledger milestone mismatch: {requirement_id}")
        if requirement_by_id[requirement_id].get("mandatory") is True:
            require(entry.get("mandatory") is True, f"mandatory flag mismatch: {requirement_id}")
        if has_proof:
            require(entry.get("status") == "pass", f"proof-backed ledger entry must be pass: {requirement_id}")
            for proof_ref in entry["proofRefs"]:
                validate_proof_ref(entry, proof_ref)
        if has_blocker:
            require(entry.get("status") == "blocked", f"blocker-backed ledger entry must be blocked: {requirement_id}")
            validate_blocker_ref(entry)

    missing = set(requirement_by_id) - seen
    require(not missing, f"evidence ledger missing requirements: {sorted(missing)}")


def relative(path: Path) -> str:
    try:
        return str(path.relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


def iter_text_files(base: Path):
    if not base.exists():
        return
    for path in base.rglob("*"):
        if path.is_file() and path.suffix.lower() in {
            ".build.cs",
            ".cmake",
            ".cpp",
            ".cs",
            ".cxx",
            ".h",
            ".hpp",
            ".hxx",
            ".md",
            ".txt",
        }:
            yield path


def cmake_target_links(cmake_text: str, target_name: str) -> list[str]:
    links: list[str] = []
    pattern = re.compile(r"target_link_libraries\s*\(\s*" + re.escape(target_name) + r"\b([\s\S]*?)\)", re.MULTILINE)
    for match in pattern.finditer(cmake_text):
        body = re.sub(r"#.*", "", match.group(1))
        links.extend(
            token
            for token in re.split(r"\s+", body)
            if token and token not in {"PUBLIC", "PRIVATE", "INTERFACE"}
        )
    return links


def validate_source_alias_tree() -> None:
    for module_name, canonical in (("CoreSim", "core_sim"), ("GeoTerrain", "geo_terrain")):
        path = REPO_ROOT / "Source" / module_name
        require(path.is_dir(), f"missing compatibility source tree: Source/{module_name}")
        readme = path / "README.md"
        require(readme.is_file(), f"Source/{module_name} must document its canonical source boundary")
        text = readme.read_text(encoding="utf-8")
        require(canonical in text, f"Source/{module_name} must point to {canonical}")


def validate_module_boundaries(root: Path = REPO_ROOT) -> None:
    for module_name, rule in MODULE_BOUNDARIES.items():
        existing_paths = [root / path for path in rule["paths"] if (root / path).exists()]
        require(existing_paths, f"missing module source tree for {module_name}")

        for base in existing_paths:
            for path in iter_text_files(base):
                text = path.read_text(encoding="utf-8", errors="ignore")
                for include in INCLUDE_RE.findall(text):
                    for prefix in rule["forbidden_include_prefixes"]:
                        if include.startswith(prefix) or prefix in include:
                            raise ValidationError(
                                f"{module_name} forbidden include {include!r} in {relative(path)}"
                            )

        for path in existing_paths:
            cmake = path / "CMakeLists.txt"
            if not cmake.is_file():
                continue
            text = cmake.read_text(encoding="utf-8")
            for target in ("flying_core_sim", "flying_geo_terrain"):
                for link in cmake_target_links(text, target):
                    for forbidden in rule["forbidden_link_tokens"]:
                        if forbidden.lower() in link.lower():
                            raise ValidationError(
                                f"{module_name} target {target} has forbidden dependency {link}"
                            )


def validate_ci_entrypoint() -> None:
    script = REPO_ROOT / "tools" / "ci" / "validate_all.py"
    require(script.is_file(), "missing CI validation entry point: tools/ci/validate_all.py")
    text = script.read_text(encoding="utf-8")
    require("cmake" in text and "--preset" in text, "validate_all.py must drive CMake presets")
    require("forge_validate.py" in text and "architecture" in text,
            "validate_all.py must run the architecture validator")


def validate_architecture_boundaries() -> None:
    validate_contract()
    validate_source_alias_tree()
    validate_ci_entrypoint()
    validate_module_boundaries()


def run_architecture_boundary_selftest() -> None:
    cases = [
        ("CoreSim Unreal include", Path("core_sim/bad.cpp"), '#include "CoreMinimal.h"\n'),
        ("CoreSim Blueprint include", Path("core_sim/bad.cpp"), '#include "Blueprint/UserWidget.h"\n'),
        ("CoreSim Cesium include", Path("core_sim/bad.cpp"), '#include "CesiumGeoreference.h"\n'),
        ("CoreSim Presentation include", Path("core_sim/bad.cpp"), '#include "FlyingPresentation/Public/FlyingPresentation.h"\n'),
        ("CoreSim Unreal path include", Path("core_sim/bad.cpp"), '#include "unreal/Source/FlyingPresentation/Public/FlyingPresentation.h"\n'),
        (
            "CoreSim Presentation link",
            Path("core_sim/CMakeLists.txt"),
            "target_link_libraries(flying_core_sim PUBLIC FlyingPresentation)\n",
        ),
        (
            "CoreSim Cesium link",
            Path("core_sim/CMakeLists.txt"),
            "target_link_libraries(flying_core_sim PUBLIC CesiumRuntime)\n",
        ),
        (
            "CoreSim Unreal link",
            Path("core_sim/CMakeLists.txt"),
            "target_link_libraries(flying_core_sim PUBLIC UnrealEngine)\n",
        ),
        (
            "CoreSim Blueprint link",
            Path("core_sim/CMakeLists.txt"),
            "target_link_libraries(flying_core_sim PUBLIC BlueprintAssets)\n",
        ),
        (
            "GeoTerrain Presentation private include",
            Path("geo_terrain/bad.cpp"),
            '#include "FlyingPresentation/Private/FlyingCoreSimComponent.h"\n',
        ),
        (
            "GeoTerrain Unreal presentation private include",
            Path("geo_terrain/bad.cpp"),
            '#include "unreal/Source/FlyingPresentation/Private/FlyingCoreSimComponent.h"\n',
        ),
        (
            "GeoTerrain Presentation private link",
            Path("geo_terrain/CMakeLists.txt"),
            "target_link_libraries(flying_geo_terrain PRIVATE FlyingPresentationPrivate)\n",
        ),
    ]

    for name, relative_path, content in cases:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "core_sim").mkdir(parents=True)
            (root / "geo_terrain").mkdir(parents=True)
            (root / "core_sim" / "CMakeLists.txt").write_text(
                "target_link_libraries(flying_core_sim PUBLIC Flying::GeoTerrain)\n",
                encoding="utf-8",
            )
            (root / "geo_terrain" / "CMakeLists.txt").write_text(
                "target_link_libraries(flying_geo_terrain PUBLIC Flying::CompilerOptions)\n",
                encoding="utf-8",
            )

            path = root / relative_path
            path.write_text(content, encoding="utf-8")

            try:
                validate_module_boundaries(root)
            except ValidationError:
                continue

        raise ValidationError(f"architecture validator accepted forbidden dependency: {name}")


def command_contract(_: argparse.Namespace) -> int:
    validate_contract()
    print("forge_validate: contract ok")
    return 0


def command_architecture(_: argparse.Namespace) -> int:
    validate_architecture_boundaries()
    print("forge_validate: architecture ok")
    return 0


def command_architecture_boundary_selftest(_: argparse.Namespace) -> int:
    run_architecture_boundary_selftest()
    print("forge_validate: forbidden module dependency rejected")
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


def command_evidence_ledger(args: argparse.Namespace) -> int:
    contract = validate_contract()
    ledger = load_json(REPO_ROOT / args.ledger)
    validate_ledger_entries(contract, ledger)
    print("forge_validate: evidence ledger ok")
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


def command_missing_requirement_selftest(_: argparse.Namespace) -> int:
    contract = validate_contract()
    valid_entries = [
        {
            "requirementId": requirement["id"],
            "milestone": requirement["milestone"],
            "mandatory": requirement["mandatory"],
            "status": "blocked",
            "blockerRef": "docs/blockers/external-inputs.yml#production-visual-shipping-evidence",
        }
        for requirement in contract["requirements"]
    ]
    bad_ledger = {
        "schemaVersion": "flying.evidence-ledger.v1",
        "contractId": contract["contractId"],
        "generatedAtUtc": "2026-08-18T00:00:00Z",
        "requirements": valid_entries[1:],
    }

    try:
        validate_ledger_entries(contract, bad_ledger)
    except ValidationError:
        print("forge_validate: missing requirement evidence rejected")
        return 0

    raise ValidationError("missing requirement evidence was accepted")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Flying 2.0 contract evidence gates.")
    subcommands = parser.add_subparsers(dest="command", required=True)

    contract = subcommands.add_parser("contract", help="validate contract registry and ledger schema")
    contract.set_defaults(func=command_contract)

    architecture = subcommands.add_parser("architecture", help="validate architecture-facing contract gates")
    architecture.set_defaults(func=command_architecture)

    architectur = subcommands.add_parser("architectur", help="compatibility alias for architecture")
    architectur.set_defaults(func=command_architecture)

    architecture_boundary_selftest = subcommands.add_parser(
        "architecture-boundary-selftest",
        help="prove forbidden module dependencies are rejected",
    )
    architecture_boundary_selftest.set_defaults(func=command_architecture_boundary_selftest)

    release_gate = subcommands.add_parser("release-gate", help="validate an evidence ledger for release")
    release_gate.add_argument("--ledger", default="docs/evidence/ledger.json")
    release_gate.set_defaults(func=command_release_gate)

    evidence_ledger = subcommands.add_parser("evidence-ledger", help="validate an evidence ledger for coverage and proof/blocker shape")
    evidence_ledger.add_argument("--ledger", default="docs/evidence/ledger.json")
    evidence_ledger.set_defaults(func=command_evidence_ledger)

    missing_evidence = subcommands.add_parser(
        "missing-evidence-selftest",
        help="prove mandatory requirements without proof or blockers are rejected",
    )
    missing_evidence.set_defaults(func=command_missing_evidence_selftest)

    missing_requirement = subcommands.add_parser(
        "missing-requirement-selftest",
        help="prove ledgers missing contract requirements are rejected",
    )
    missing_requirement.set_defaults(func=command_missing_requirement_selftest)

    args = parser.parse_args()
    try:
        return args.func(args)
    except ValidationError as exc:
        print(f"forge_validate: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
