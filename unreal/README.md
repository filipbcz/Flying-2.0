# Unreal Integration

This directory contains the Unreal Engine 5.8 Win64 presentation project.

The `FlyingPresentation` runtime module enables Cesium for Unreal, uses
`ACesiumGeoreference` for ECEF-to-Unreal coordinate transforms, loads pilot
terrain and imagery package manifests from local paths, and drives a basic
aircraft actor from the standalone CoreSim state. Runtime map APIs and Cesium
ion assets are intentionally not configured.

Default local package paths are configured in `Config/DefaultGame.ini` and are
expected to point at generated outputs from the existing data pipeline:

- `TerrainPackageManifestPath`: `flying.terrain-package.v1`
- `PilotRegionPackageManifestPath`: `flying.pilot-region-package.v1`

The Unreal presentation code treats CoreSim ECEF coordinates as authoritative.
World-origin shifts recompute actor presentation transforms from the last
CoreSim snapshot instead of rewriting CoreSim state.
