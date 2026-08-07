# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 24. Implement Offline 2D Navigation Map
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
24. Implement Offline 2D Navigation Map

Step description and scope:
Build the runtime 2D map using local vector tiles from ZABAGED, Geonames, airports, runways, obstacles, and permitted airspace data, with local style definitions and no runtime dependency on external APIs.

In scope:
- Local vector tile generation integration
- Runtime map renderer
- Layer toggles
- Aircraft and replay overlays
- Offline attribution display
- Map style

Out of scope:
- External public tile servers
- Online route planning
- Paid API integrations
- Global map coverage

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

Future roadmap steps (explicitly out of scope):
- 25. Implement World Objects, Vegetation, Water, Obstacles, And Audio
- 26. Implement Save, Scenario Editor, Diagnostics, And Failure Workflows
- 27. Optimize Performance And Long-Run Stability
- 28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics
- 29. Produce Licensing, SBOM, Documentation, And Release Evidence
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- 2D map loads all required base and aviation layers from local MBTiles or PMTiles packages.
- Map can toggle airports, runways, obstacles, airspaces, labels, aircraft position, flight path, and replay track.
- Runtime map initialization succeeds with network disabled and without API keys.
- Map attribution is visible in the application and matches source-license documentation.

## Agent Configuration
- no project config provided, using defaults