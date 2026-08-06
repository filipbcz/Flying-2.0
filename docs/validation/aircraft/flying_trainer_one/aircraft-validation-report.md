# Flying Trainer One Aircraft Validation Report

Status: Formal validation package present; type-fidelity validation blocked by missing credible reference data

Date: 2026-08-06

Aircraft model: `flying_trainer_one`

Suite: `flying.aircraft_validation_suite.v1`

## Decision

`flying_trainer_one` is not labeled faithful to a selected real-world aircraft type. The aircraft remains an unbranded project-authored trainer because the repository does not contain licensed POH/AFM, flight-test, CFD, wind-tunnel, or equivalent credible reference data for a named aircraft.

Release-blocking validation status is `passed_with_approved_exclusions`: every mandatory category has a scenario definition and result record, and each missing reference-backed tolerance is explicitly excluded by `docs/governance/m0-aircraft-data-gate.md`.

## Coverage

The suite covers stall speeds, maximum and cruise speeds, climb rate, service ceiling, glide, sink rate, takeoff distance, landing distance, engine RPM/power/fuel/temperature points, stability modes, control-step responses, coordinated turns, ground effect, crosswind, tire slip, and braking.

## Data Records

- Reference registry: `reference-registry.json`
- Scenario definitions: `validation-scenarios.json`
- Result records: `validation-results.json`
- Deviation graph data: `graphs/*.csv`
- Executable release gate: `tools/validate_aircraft_validation_suite.mjs`

## Release Gate

The executable gate fails when a mandatory reference-backed result exceeds documented tolerances. It also fails if the aircraft is labeled as a faithful type simulation while any mandatory test is failed or excluded.

Current results contain only approved exclusions for missing credible type-reference data. No tolerance is invented from the project-authored model.
