# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 15. Build Vertical Slice Flight And Performance Tests
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
15. Build Vertical Slice Flight And Performance Tests

Step description and scope:
Create automated and manual validation for the M1 vertical slice: pilot-region loading, takeoff, circuit, landing, replay, terrain continuity, CoreSim timing, and basic performance budgets.

In scope:
- M1 automated test suite
- Scripted takeoff-circuit-landing scenario
- Pilot-region performance capture
- M1 validation report
- Known limitations document

Out of scope:
- Whole-country performance certification
- Final aircraft validation
- Installer signing
- Full airport coverage

Execution boundary:
- Implement only the current step and its acceptance criteria.
- Do not implement work assigned to future roadmap steps.
- Reuse existing functionality. If part of this step is already satisfied, verify it instead of rewriting it.
- Keep unrelated repository files unchanged.

Already completed roadmap steps (existing repository context):
- 1. Freeze Product Scope And Legal Data Gate
- 2. Establish Repository Build Skeleton
- 3. Implement CoreSim Fixed-Step Kernel
- 4. Implement Geodesy And Units Library
- 5. Integrate JSBSim Into CoreSim
- 6. Build Terrain Height Service Contract
- 7. Create GIS Data Pipeline Foundation
- 8. Implement DMR 5G Terrain Processing For Pilot Region
- 9. Implement Ortofoto And Vector Package Processing For Pilot Region
- 10. Implement Airport Master List And Runway Schema
- 11. Implement Runway Importer And Pilot Airport Surfaces
- 12. Create Unreal UE 5.8 Project And Cesium Runtime Integration
- 13. Implement Input Device Mapping And Scenario Start Flow
- 14. Implement Telemetry, Replay, And Export V1

Future roadmap steps (explicitly out of scope):
- 16. Complete Production Aircraft Data Model
- 17. Implement Aircraft Systems And Sensor Models
- 18. Implement Cockpit And Aircraft Presentation
- 19. Implement Weather And Atmosphere Coupling
- 20. Build Aircraft Validation Suite
- 21. Scale Terrain Pipeline To Full Czech Republic
- 22. Complete Airport And SLZ Coverage
- 23. Implement Detailed Airport Set
- 24. Implement Offline 2D Navigation Map
- 25. Implement World Objects, Vegetation, Water, Obstacles, And Audio
- 26. Implement Save, Scenario Editor, Diagnostics, And Failure Workflows
- 27. Optimize Performance And Long-Run Stability
- 28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics
- 29. Produce Licensing, SBOM, Documentation, And Release Evidence
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- Automated test suite runs CoreSim unit tests, geodesy tests, terrain service tests, runway importer tests, replay hash tests, and pilot package validation.
- A scripted pilot-region flight can taxi, take off, fly a circuit, land, stop, and produce telemetry without loading screens.
- Performance capture for the reference-class PC reports FPS, 1% low, CoreSim step misses, input latency, hitch count, RAM, and VRAM for the vertical slice.
- M1 report documents pass/fail status and known limitations without marking unvalidated aircraft fidelity as complete.

## Agent Configuration
- no project config provided, using defaults