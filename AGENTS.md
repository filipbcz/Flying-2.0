# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 27. Optimize Performance And Long-Run Stability
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
27. Optimize Performance And Long-Run Stability

Step description and scope:
Profile and optimize rendering, terrain streaming, object density, CoreSim timing, input latency, memory use, hitching, and long-duration stability on the reference Windows 11 hardware class.

In scope:
- Profiling
- Rendering optimization
- Terrain streaming optimization
- Object density scaling
- CoreSim timing validation
- Input latency measurement
- Memory/VRAM budget enforcement
- Ten-hour soak

Out of scope:
- Supporting below-minimum hardware
- VR performance
- Force feedback performance
- Non-Windows platforms

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

Future roadmap steps (explicitly out of scope):
- 28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics
- 29. Produce Licensing, SBOM, Documentation, And Release Evidence
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- High graphics profile reaches at least 60 FPS at 2560 x 1440 over ordinary Czech terrain on the reference hardware class.
- Measured 1% low FPS is at least 45, input latency is below 50 ms at 60 FPS, and no normal-streaming hitch exceeds 100 ms in approved test routes.
- CoreSim records no missed 240 Hz physics steps during approved performance scenarios.
- Ten-hour automated flight soak test completes without crash and within 24 GB RAM and 10 GB VRAM limits.

## Agent Configuration
- no project config provided, using defaults