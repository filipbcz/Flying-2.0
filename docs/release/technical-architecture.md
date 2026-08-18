# Flying Technical Architecture

Document ID: REL-29-TECH-ARCH  
Status: Release evidence candidate  
Scope: Architecture evidence for the Flying 1.0.0 Win64 offline release family

## System Shape

Flying is an offline-first Win64 simulator with a deterministic native simulation core, native GIS/data-package tooling, and an Unreal Engine 5.8 presentation runtime. Runtime flight must not depend on external map APIs, public tile servers, online aeronautical services or required network credentials.

## Major Components

| Component | Repository path | Responsibility |
| --- | --- | --- |
| CoreSim | `core_sim/` | Fixed-step simulation kernel, aircraft configuration ingestion, systems and sensor models, scenario state, weather coupling, terrain contact, telemetry and replay contracts. |
| GeoTerrain | `geo_terrain/` | Geodesy, units and terrain height service contracts used by CoreSim and data tooling. |
| Data Pipeline | `data_pipeline/` | Source manifest validation, CUZK terrain processing, Ortofoto/ZABAGED/Geonames package processing, airport/runway import, offline navigation map package generation and package lineage metadata. |
| Unreal CoreSim Bridge | `unreal/Source/FlyingCoreSimBridge/` | UE module boundary that links the native CoreSim and GeoTerrain implementation into the Win64 Unreal target. |
| Unreal Presentation | `unreal/Source/FlyingPresentation/` | UE 5.8 runtime presentation module, Cesium georeference bridge, procedural terrain actor, aircraft actor, cockpit/presentation widgets, input mapping, scenario screens, replay and diagnostics UI. It consumes CoreSim through `FlyingCoreSimBridge` and must not own native physics or geodesy implementation files. |
| Packaging | `packaging/` | Reproducible Win64 Shipping build, signing, installer, update/repair and manifest hashing scripts. |
| Release Evidence | `docs/release/` | SBOM, license inventory, user manual, architecture, data protocol, known limitations and QA evidence index. |

## Runtime Flow

1. The installed application reads build metadata and presentation settings.
2. Local terrain, GIS and navigation map manifests are resolved relative to the installation.
3. Runtime package readers reject missing files, remote URL tokens, map API keys and required network dependencies.
4. Scenario selection creates a CoreSim scenario start request with a selected location and start mode.
5. CoreSim advances at a fixed step behind the Unreal CoreSim bridge and publishes immutable state snapshots.
6. Unreal presentation maps ECEF state snapshots through Cesium georeferencing and renders cockpit, aircraft, terrain, instruments, audio, diagnostics and map overlays.
7. Telemetry and replay record build, aircraft, scenario, input and data-package identity for compatibility checks.

## Data Boundaries

Source GIS data is processed outside the game into versioned local packages. Runtime code reads only generated packages and manifests. Source manifests must include provenance, license, attribution, checksums, CRS, permitted-use declarations and source lineage. Generated package manifests carry source lineage forward into the installed product.

CUZK-derived packages are treated as adapted CC BY 4.0 data. AIM/AIP/VFR automated import and redistribution remain blocked unless written permission is archived. Operator data must carry written source confirmation and permitted-use metadata.

## Packaging Boundary

The packaging scripts generate a Win64 Shipping build, copy runtime metadata and terrain packages, sign binaries and installer artifacts, and emit a release manifest with SHA-256 hashes. The repair tool verifies the installed release manifest and restores missing or corrupted files from local media or a local package cache; it does not download data by default.

## Diagnostics And Privacy

Shipping builds include local crash minidump capture, structured logs and build metadata. Default configuration disables implicit crash upload, contact prompts and log upload. Crash telemetry remains off unless the user explicitly opts in through configuration.

## Certification Boundary

The architecture supports deterministic QA and release evidence. It does not provide certification as a flight training device, official aeronautical database, official navigation product or faithful model of any named aircraft type.
