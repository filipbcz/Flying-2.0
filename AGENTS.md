# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 11. Implement Runway Importer And Pilot Airport Surfaces
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
11. Implement Runway Importer And Pilot Airport Surfaces

Step description and scope:
Build the runway importer that creates georeferenced runway, taxi connection, start-point, material, collision, marking, and LOD data for the two pilot airports from approved seed records and open/approved sources.

In scope:
- Runway importer
- Pilot paved runway surface
- Pilot grass runway surface
- Collision mesh
- Safe start positions
- Basic runway markings
- Terrain transition smoothing
- Pilot coverage report

Out of scope:
- All Czech airports
- Detailed terminal/hangar modeling
- Full lighting system
- AI traffic or ATC

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

Future roadmap steps (explicitly out of scope):
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
- Importer builds runway geometry from published or verified threshold coordinates, not from ARP plus runway name and length alone.
- Generated pilot runway surfaces preserve longitudinal and transverse slope instead of flattening all runways to one horizontal plane.
- Runway collision surface takes priority over generic terrain and matches visual runway surface within 0.05 m in wheel-contact zones.
- Pilot airport validation reports include coordinate, heading, dimension, Ortofoto alignment, terrain transition, and provenance checks.

## Agent Configuration
- no project config provided, using defaults