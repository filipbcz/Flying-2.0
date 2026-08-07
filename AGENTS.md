# AGENTS.md

## Project
- name: Flying
- repo: filipbcz/flying
- default branch: main

## Task
- title: Flying: 28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics
- mode: full_auto
- max iterations: 10

## Current Step Context
Current implementation step:
28. Implement Packaging, Installer, Signing, Updates, And Crash Diagnostics

Step description and scope:
Create the reproducible Win64 Shipping build pipeline, installer, terrain package installation, code signing, update and data-repair flow, minidump capture, structured logging, and unique build ID reporting.

In scope:
- Shipping build automation
- Installer
- Code signing integration
- Terrain package installation
- Update/data repair mechanism
- Minidumps
- Structured logging
- Build ID

Out of scope:
- Steam or store publishing
- Mandatory online updater
- Cloud telemetry by default
- macOS/Linux builds

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
- 27. Optimize Performance And Long-Run Stability

Future roadmap steps (explicitly out of scope):
- 29. Produce Licensing, SBOM, Documentation, And Release Evidence
- 30. Execute Release Candidate Acceptance Gate

Acceptance Criteria:
- Win64 Shipping build is reproducible from documented commands and includes simulator binaries, required runtime content, and version metadata.
- Installer installs the simulator and selected Czech terrain packages on a clean supported Windows 11 machine without requiring network access for normal flight.
- Application binaries and installer are signed, and build ID is visible in logs, diagnostics, crash reports, and about screen.
- Crash diagnostics produce minidumps and structured logs with no personal telemetry unless user opt-in is explicitly recorded.

## Agent Configuration
- no project config provided, using defaults