# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 23. Implement Detailed Airport Set
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
23. Implement Detailed Airport Set

Step description and scope:
Create the detailed airport content manifest and implement enhanced scenery for controlled public airports and validation-scenario airports: taxiways, aprons, stands, lighting, signs, buildings, hangars, windsocks, markings, and significant obstacles.

In scope:
- Detailed airport manifest
- Controlled public airport details
- Validation airport details
- Lighting systems
- Signs and markings
- Buildings and hangars
- Significant obstacles
- Collision integration

Out of scope:
- Photogrammetric city reconstruction
- Full handcrafted detail for every SLZ field
- ATC systems
- Ground traffic AI

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

Future roadmap steps (explicitly out of scope):
- 24. Implement Offline 2D Navigation Map
- 25. Implement World Objects, Vegetation, Water, Obstacles, And Audio
- 26. Implement Save, Scenario Editor, Diagnostics, And Failure Workflows
- 27. Optimize Performance And Long-Run Stability
- 28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics
- 29. Produce Licensing, SBOM, Documentation, And Release Evidence
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- Detailed-airport manifest names all controlled public airports and all airports used by validation scenarios.
- Each detailed airport includes taxiways, stands, aprons, runway/taxi signage, relevant buildings, windsock placement, lighting systems where applicable, and significant approach/departure obstacles.
- Runway lighting, PAPI/VASI geometry, markings, and declared-distance data match the effective approved source version or are flagged as derived with review approval.
- Detailed airport collision and terrain transition tests pass without runway/terrain height discontinuities in aircraft wheel-contact areas.

## Agent Configuration
- no project config provided, using defaults