# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 25. Implement World Objects, Vegetation, Water, Obstacles, And Audio
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
25. Implement World Objects, Vegetation, Water, Obstacles, And Audio

Step description and scope:
Generate and stream procedural buildings, vegetation, water surfaces, obstacles, power lines, windsocks, and environment audio from approved vector and terrain data with collision limited to active safety zones.

In scope:
- Procedural placement rules
- Building and vegetation generation
- Water rendering/collision metadata
- Flight-critical obstacles
- Active-zone collision management
- Environment audio hooks
- Graphics-density scaling

Out of scope:
- Nationwide photogrammetric 3D cities
- Random object placement from imagery only
- Full collision for every distant object
- Wildlife or non-flight decorative systems

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

Future roadmap steps (explicitly out of scope):
- 26. Implement Save, Scenario Editor, Diagnostics, And Failure Workflows
- 27. Optimize Performance And Long-Run Stability
- 28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics
- 29. Produce Licensing, SBOM, Documentation, And Release Evidence
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- Buildings and vegetation are placed from approved vector data and DMP-derived height estimates, not from ortho color inference alone.
- Flight-critical obstacles, masts, power lines, windsocks, runway objects, and water bodies are represented with appropriate visibility and active-zone collision behavior.
- Vegetation and object density scales through graphics profiles without breaking collision rules for flight-critical objects.
- Distant terrain and object streaming avoid visible horizon instability or disruptive popping in approved test routes.

## Agent Configuration
- no project config provided, using defaults