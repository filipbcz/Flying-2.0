# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 6. Build Terrain Height Service Contract
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
6. Build Terrain Height Service Contract

Step description and scope:
Implement the unified Terrain Height Service API and local test backend returning height, normal, surface type, collision metadata, and runway override priority to both CoreSim and presentation clients.

In scope:
- Terrain service interface
- In-memory test backend
- Surface material taxonomy
- Runway override priority rules
- CoreSim query adapter
- Unit tests

Out of scope:
- DMR 5G production import
- Cesium 3D Tiles generation
- Full airport database
- Visual terrain rendering

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

Future roadmap steps (explicitly out of scope):
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
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- Terrain Height Service exposes one API for height, surface normal, surface material, collision availability, source tile identity, and confidence metadata.
- CoreSim can query terrain contact data through the service without depending on Cesium or Unreal types.
- Service supports runway-surface overrides with higher authority than generic DEM terrain.
- Tests cover flat, sloped, discontinuous, and runway-over-generic-terrain cases.

## Agent Configuration
- no project config provided, using defaults