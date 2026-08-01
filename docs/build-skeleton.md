# Repository Build Skeleton

Document ID: ENG-M1-BUILD-SKELETON  
Status: Initial skeleton  
Scope: Directory layout, native build presets, CI entry points and empty module boundaries only.

## Top-Level Areas

| Path | Responsibility | Current contents |
| --- | --- | --- |
| `core_sim/` | Standalone native CoreSim boundary. | Placeholder C++ library. |
| `geo_terrain/` | Shared geodesy and terrain authority boundary. | Placeholder C++ library. |
| `data_pipeline/` | Offline GIS and package-processing tool boundary. | Placeholder C++ library and future tools marker. |
| `unreal/` | Unreal Engine presentation and integration boundary. | Future presentation module marker only; no UE project yet. |
| `tests/` | Native tests. | Skeleton smoke test. |
| `tools/` | Local developer/CI entry points. | Narrow validation script. |
| `docs/` | Architecture, governance and build documentation. | M0 records and this skeleton note. |
| `packaging/` | Future installer, signing and package assembly boundary. | Documentation placeholder only. |
| `third_party/` | Dependency manifest and notices. | Version intent and notice placeholders. |

## Build Presets

The root `CMakePresets.json` defines these named presets:

| Preset | Purpose |
| --- | --- |
| `development` | Debug native build for day-to-day skeleton work. |
| `test` | Debug native build used by CI and local smoke validation. |
| `win64-packaging` | Release native configuration for future Windows x64 package assembly. |

All presets keep `FLYING_RUNTIME_NETWORK` and `FLYING_REQUIRE_MAP_API_KEYS` off. The build must not require external map APIs, public tile services or runtime credentials.

## Local Validation

Run the narrow skeleton check from the repository root:

```sh
tools/validate.sh
```

The script configures the `test` preset, builds the empty native modules and runs the smoke test. It requires a local CMake and C++ toolchain but does not install dependencies or run broad validation suites.
