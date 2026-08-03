# FlyingPresentation

Runtime presentation module for the Unreal Engine 5.8 Win64 project.

Implemented boundaries:

- `UFlyingCoreSimComponent` owns the standalone CoreSim bridge and publishes
  immutable ECEF snapshots for presentation.
- `UFlyingCesiumGeoreferenceComponent` maps CoreSim ECEF positions and
  directions through Cesium for Unreal.
- `AFlyingOfflinePilotTerrainActor` reads local terrain and pilot-region GIS
  package manifests, refuses runtime map dependencies, and renders CSV terrain
  tiles as procedural meshes with PPM imagery-derived vertex colors.
- `AFlyingCoreSimAircraftActor` follows CoreSim snapshots and handles Unreal
  world-origin shifts by recomputing from the authoritative ECEF state.
