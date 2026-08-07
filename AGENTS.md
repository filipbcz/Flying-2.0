# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 30. Execute Release Candidate Acceptance Gate
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
30. Execute Release Candidate Acceptance Gate

Step description and scope:
Run the final integrated release candidate gate on a clean Windows 11 environment using the signed Shipping build, installed Czech terrain, approved airport database, validated aircraft, and complete offline test workflow.

In scope:
- Final clean-machine acceptance
- Integrated release test run
- Coverage report review
- Validation report review
- Performance and soak evidence
- Release gate decision

Out of scope:
- Implementing new features during the gate
- Expanding beyond Czech Republic
- Adding additional aircraft
- Waiving failed mandatory tests without documented approval

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
- 15. Build Vertical Slice Flight And Performance Tests
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

Acceptance Criteria:
- Clean-machine test demonstrates install, terrain installation, cold-and-dark start, engine start, taxi, takeoff, cross-country flight over streamed Czech terrain, landing at another included airport, shutdown, replay, and telemetry export.
- All automated CoreSim, geodesy, terrain, runway, airport coverage, weather, replay, packaging, license, performance, and soak tests pass for the release candidate build.
- Coverage report has zero missing active runways against the approved master list and no production-validated runway with incomplete required provenance.
- Release candidate is blocked if any mandatory physics, terrain, airport, licensing, offline-operation, installer, stability, or performance acceptance test fails.

## Agent Configuration
- no project config provided, using defaults