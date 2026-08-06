# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 19. Implement Weather And Atmosphere Coupling
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
19. Implement Weather And Atmosphere Coupling

Step description and scope:
Implement the numerical atmosphere and weather model shared by CoreSim, visual effects, vegetation, particles, and sound, including wind profiles, gusts, turbulence, pressure, temperature, humidity, clouds, precipitation, visibility, icing, and wet surfaces.

In scope:
- Atmosphere model
- Wind field
- Gust and turbulence model
- Manual weather UI/data
- Optional live weather adapter interface
- Weather-to-physics coupling
- Weather visuals integration

Out of scope:
- Mandatory online weather
- Multiplayer weather synchronization
- Unverified aquaplaning model
- Global weather outside Czech coverage

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

Future roadmap steps (explicitly out of scope):
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
- CoreSim receives numerical wind, turbulence, pressure, temperature, density, and humidity data independent of weather visuals.
- Weather model supports manual scenarios and a clearly optional METAR/GRIB adapter path that is disabled or unavailable without explicit user action.
- Gust and turbulence tests validate deterministic Dryden or von Karman model behavior for fixed seeds and scenario inputs.
- Enabled clouds, precipitation, icing, and wet surfaces have corresponding physical effects on aircraft, sensors, runway friction, visibility, or engine behavior.

## Agent Configuration
- no project config provided, using defaults