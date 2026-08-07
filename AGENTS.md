# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 26. Implement Save, Scenario Editor, Diagnostics, And Failure Workflows
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
26. Implement Save, Scenario Editor, Diagnostics, And Failure Workflows

Step description and scope:
Add complete user workflow support for scenario creation, save/load, failure setup, diagnostics overlay, post-flight route map, telemetry graph viewing, and robust save-data handling.

In scope:
- Scenario editor
- Save/load system
- Failure setup UI
- Diagnostics overlay
- Post-flight route map
- Telemetry graph UI
- Atomic save handling

Out of scope:
- Cloud saves
- Multiplayer scenarios
- Training certification workflows
- New aircraft types

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

Future roadmap steps (explicitly out of scope):
- 27. Optimize Performance And Long-Run Stability
- 28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics
- 29. Produce Licensing, SBOM, Documentation, And Release Evidence
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- User can configure aircraft, airport or position, date, time, weather, weight, fuel, and failures before flight.
- Save and settings files are written atomically and corrupted files are detected with recovery behavior that avoids application crashes.
- Diagnostics display authoritative position, altitude variants, CoreSim timing, terrain source tile, weather values, input state, and build/data versions.
- Post-flight screen provides replay access, route map, telemetry graphs, and CSV/JSON export entry points.

## Agent Configuration
- no project config provided, using defaults