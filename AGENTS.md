# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 22. Complete Airport And SLZ Coverage
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
22. Complete Airport And SLZ Coverage

Step description and scope:
Populate and validate the full approved master list of active Czech airports, active runways, AIP/VFR airports, and VFR-published SLZ fields using permitted sources and manual verification where required.

In scope:
- Full airport database population
- SLZ field records
- Runway geometry verification
- Coverage report
- Manual verification workflow
- AIRAC/source diff reports
- Start-location eligibility rules

Out of scope:
- Restricted AIP/VFR redistribution without permission
- Detailed art pass for every airport
- Multiplayer ATC
- Historical airport mode implementation

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

Future roadmap steps (explicitly out of scope):
- 23. Implement Detailed Airport Set
- 24. Implement Offline 2D Navigation Map
- 25. Implement World Objects, Vegetation, Water, Obstacles, And Audio
- 26. Implement Save, Scenario Editor, Diagnostics, And Failure Workflows
- 27. Optimize Performance And Long-Run Stability
- 28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics
- 29. Produce Licensing, SBOM, Documentation, And Release Evidence
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- Coverage report compares the generated airport database against the approved master list and reports zero missing active validated runways for release scope.
- Each production-validated runway has verified threshold coordinates, heading, length, width, surface, declared distances where available, provenance, AIRAC/source effective date, confidence, and manual verification state.
- Closed or temporarily unavailable aerodromes are not offered as default start locations unless explicitly marked for a separate historical mode.
- Importer prevents automatic overwrite of manually verified runway geometry without review.

## Agent Configuration
- no project config provided, using defaults