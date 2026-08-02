# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 5. Integrate JSBSim Into CoreSim
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
5. Integrate JSBSim Into CoreSim

Step description and scope:
Integrate JSBSim as the initial flight dynamics backend inside CoreSim while preserving CoreSim ownership of stepping, state transfer, deterministic inputs, and headless execution.

In scope:
- JSBSim dependency integration
- CoreSim-to-JSBSim adapter
- Placeholder aircraft config for infrastructure
- State extraction API
- Repeatability regression test

Out of scope:
- Claiming fidelity for the production aircraft
- Aircraft validation tolerance sign-off
- Cockpit systems
- Terrain collision coupling

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

Future roadmap steps (explicitly out of scope):
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
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- JSBSim can be stepped from CoreSim at 240 Hz through a stable adapter API.
- CoreSim remains runnable headless without Unreal or GPU dependencies.
- Adapter records input controls, aircraft configuration identity, and output state required by telemetry and replay.
- Regression tests demonstrate repeatable results for a placeholder/community aircraft model used only for infrastructure testing.

## Agent Configuration
- no project config provided, using defaults