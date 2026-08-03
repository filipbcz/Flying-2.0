# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 8. Implement DMR 5G Terrain Processing For Pilot Region
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
8. Implement DMR 5G Terrain Processing For Pilot Region

Step description and scope:
Extend the data pipeline to ingest a 50 x 50 km pilot area from DMR 5G, transform coordinates and height systems, clean tile boundaries, generate terrain LOD packages, and emit physical collision tiles.

In scope:
- DMR 5G pilot ingest
- Coordinate and height transformation
- LOD terrain package generation
- Physical collision tile generation
- Normals and terrain metadata
- Control-point and edge validation

Out of scope:
- Whole-country processing
- Ortofoto imagery
- Airport-specific runway mesh generation
- Runtime Cesium integration beyond package format needs

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

Future roadmap steps (explicitly out of scope):
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
- Pilot-region DMR 5G source tiles are transformed into the project coordinate and height model with recorded PROJ/geoid configuration.
- Generated terrain package contains render LOD metadata and separate physical collision tiles for the active aircraft zone.
- Automated control-point validation reports transformed terrain heights within 0.10 m above declared source error for selected test points.
- Adjacent generated pilot tiles pass an automated edge-continuity test with no cracks or unintended height steps.

## Agent Configuration
- no project config provided, using defaults