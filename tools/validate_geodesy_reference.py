#!/usr/bin/env python3
"""Targeted validation for the geodesy and boundary unit conversion step."""

from __future__ import annotations

import pathlib
import shutil
import subprocess
import sys
import json


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = pathlib.Path("/tmp/flying-geodesy-check")
TEST_NAME = "flying.geodesy.reference"
PASS_MARKERS = [
    "PASS wgs84_ecef_reference_vectors_match",
    "PASS local_and_body_frames_round_trip",
    "PASS altitude_datums_are_explicit",
    "PASS core_sim_boundary_units_match_references",
]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, cwd=REPO_ROOT, check=True, text=True)


def captured(command: list[str]) -> str:
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    print(result.stdout, end="")
    return result.stdout


def validate_static_contract() -> None:
    required_paths = [
        REPO_ROOT / "Source/GeoTerrain/Public/Geodesy.h",
        REPO_ROOT / "Source/CoreSim/Public/Units.h",
        REPO_ROOT / "Tests/GeodesyReferenceTests.cpp",
    ]
    for path in required_paths:
        require(path.is_file(), f"missing required deliverable path: {path.relative_to(REPO_ROOT)}")

    misplaced_paths = [
        REPO_ROOT / "tests/GeodesyReferenceTests.cpp",
        REPO_ROOT / "tests/geodesyreferencetests.cpp",
        REPO_ROOT / "source/coresim/public/units.h",
        REPO_ROOT / "source/geoterrain/public/geodesy.h",
        REPO_ROOT / "source/GeoTerrain/Public/Geodesy.h",
        REPO_ROOT / "source/CoreSim/Public/Units.h",
    ]
    for path in misplaced_paths:
        require(not path.exists(), f"mis-cased duplicate deliverable path exists: {path.relative_to(REPO_ROOT)}")

    source_units = (REPO_ROOT / "Source/CoreSim/Public/Units.h").read_text()
    legacy_units = (REPO_ROOT / "core_sim/include/flying/core_sim/units.hpp").read_text()
    core_sim_public = (REPO_ROOT / "Source/CoreSim/Public/CoreSim.h").read_text()
    reference_test = (REPO_ROOT / "Tests/GeodesyReferenceTests.cpp").read_text()

    for token in [
        "meters_from_feet",
        "meters_from_nautical_miles",
        "meters_per_second_from_knots",
        "pascals_from_hectopascals",
    ]:
        require(token in source_units, f"CoreSim/Public/Units.h missing boundary conversion: {token}")
        require(token not in legacy_units, f"parallel CoreSim boundary conversion remains in legacy units.hpp: {token}")

    require('#include "Units.h"' in core_sim_public, "CoreSim.h must expose Source/CoreSim/Public/Units.h")
    require('#include "Units.h"' in reference_test, "reference test must verify CoreSim/Public/Units.h")

    presets = json.loads((REPO_ROOT / "CMakePresets.json").read_text())
    smoke_build = next(
        (preset for preset in presets["buildPresets"] if preset.get("name") == "smoke"),
        None,
    )
    require(smoke_build is not None, "CMakePresets.json missing smoke build preset")
    smoke_targets = smoke_build.get("targets", [])
    require(
        "flying_geodesy_reference_tests" in smoke_targets,
        "smoke build preset must build flying_geodesy_reference_tests before ctest --preset smoke",
    )


def validate_registered_and_executed() -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)

    run([
        "cmake",
        "-S",
        ".",
        "-B",
        str(BUILD_DIR),
        "-DFLYING_BUILD_TESTS=ON",
        "-DFLYING_CORE_SIM_ENABLE_JSBSIM=OFF",
    ])
    run(["cmake", "--build", str(BUILD_DIR), "--target", "flying_geodesy_reference_tests", "--parallel", "2"])

    discovery = captured(["ctest", "--test-dir", str(BUILD_DIR), "-N", "-R", f"^{TEST_NAME}$"])
    require(TEST_NAME in discovery, f"{TEST_NAME} was not discovered by CTest")

    execution = captured(["ctest", "--test-dir", str(BUILD_DIR), "-V", "-R", f"^{TEST_NAME}$"])
    for marker in PASS_MARKERS:
        require(marker in execution, f"missing reference check output: {marker}")

    print("\nGEODESY_REFERENCE_VALIDATION_EVIDENCE")
    print(f"DISCOVERED {TEST_NAME}")
    print(f"EXECUTED {TEST_NAME}")
    for marker in PASS_MARKERS:
        print(marker)


def main() -> int:
    validate_static_contract()
    validate_registered_and_executed()
    return 0


if __name__ == "__main__":
    sys.exit(main())
