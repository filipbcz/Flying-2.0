# Third-Party Notices

This repository does not vendor third-party binary artifacts or source data. Release packages must combine this notice file with exact notices from installed engine/plugins/tools and generated data package manifests.

The release license inventory is `docs/release/license-inventory.md`; the SBOM candidate is `docs/release/sbom.spdx.json`.

## Unreal Engine And Engine Plugins

- Intended engine version: Unreal Engine 5.8.x.
- Enabled plugins/modules include Cesium for Unreal, ProceduralMeshComponent, UMG, SQLiteCore, Json, JsonUtilities and Projects.
- License: Unreal Engine EULA and applicable plugin notices from the installed engine/plugin distribution.
- Repository status: not vendored. Developers install engine materials through approved channels.
- Release requirement: preserve Epic/Unreal and installed plugin notices in the shipped notice bundle.

## Cesium for Unreal

- Intended version range: `>=2.28.0 <3.0.0`.
- Role: Unreal georeferencing, WGS-84/ECEF integration and 3D Tiles runtime layer.
- Repository status: enabled in `unreal/Flying.uproject`, not vendored here.
- Release requirement: include license and notices from the installed Cesium for Unreal plugin.

## JSBSim

- Intended integration version: JSBSim 1.2.3, tag `v1.2.3`, commit `570e8115a102df8f877b11e0e59b964ea483e3c0`.
- License: LGPL-2.1-or-later per the upstream JSBSim project.
- Repository status: not vendored. CoreSim builds the JSBSim adapter when installed development files are present, or when the explicit `FLYING_CORE_SIM_FETCH_JSBSIM` CMake option is enabled.
- Local placeholder model: `core_sim/jsbsim/aircraft/flying_placeholder` is project-authored infrastructure test data only and does not claim fidelity to any named aircraft.

## PROJ

- Intended version: 9.5.x.
- Role: offline CRS transformation support for GIS pipeline and geodesy tooling.
- Repository status: not vendored here.
- Release requirement: record exact installed version, license and grid-data notices when used by release packaging.

## Test And Build Tools

- Catch2 3.7.x is the intended native test framework when redistributed or vendored.
- CMake 3.25 or later is required by the native build skeleton.
- Inno Setup 6 and Windows SDK signing tools are used by the Win64 packaging flow.
- These tools are build-time dependencies unless a release explicitly redistributes them.

## Data Notices

Flying packages that contain CUZK-derived terrain, imagery, vector or label data must include CUZK attribution in application notices, user documentation, release notices and machine-readable package manifests.

Canonical CUZK attribution is recorded in `docs/governance/m0-legal-data-gate.md`. Release packages must identify included CUZK datasets, CC BY 4.0, the CUZK Geoportal source URL, and the fact that data were transformed, tiled, generalized and/or otherwise adapted for Flying. Release text must not imply CUZK endorsement.

AIM/AIP/VFR content, AIM terrain datasets and AIM obstacle datasets must not be imported, embedded or redistributed unless written permission and required attribution notices are archived. Operator-provided airport/runway/SLZ notices must be included when those sources are used.
