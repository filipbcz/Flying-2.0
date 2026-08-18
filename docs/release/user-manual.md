# Flying User Manual

Document ID: REL-29-USER-MANUAL  
Status: Release evidence candidate  
Audience: End users and release QA operators  
Scope: Flying 1.0.0 Win64 offline release family

## Installation

Flying is distributed as a signed Win64 installer built by the packaging flow in `packaging/README.md`. Install on Windows 11 x64 with enough local storage for the simulator and selected regional Czech terrain packages. The installer places simulator binaries, cooked Unreal content, build metadata, the selected region manifest, local terrain packages, offline GIS packages and the offline navigation package on disk under the configured `FLYING_DATA_ROOT`. Normal flight does not require an online map service or runtime API key.

If the installer reports a signature or manifest failure, stop installation and obtain a replacement release package. Do not copy terrain package files from an untrusted source.

## First Start

Start Flying from the Start menu entry created by the installer. The startup flow loads local build metadata, the selected region manifest and package manifests, then presents scenario selection. If required terrain, GIS or navigation packages are missing, use the installed repair tool before flying.

## Controls

Flying supports keyboard, mouse, gamepad and common USB/HID flight-controller input through the Unreal presentation input mapping subsystem. Use the input calibration screen before first flight and after changing controllers.

Primary flight controls:

| Control | Function |
| --- | --- |
| Pitch axis | Elevator. |
| Roll axis | Ailerons. |
| Yaw axis | Rudder. |
| Throttle axis | Engine throttle. |
| Mixture axis | Mixture, when mapped. |
| Propeller axis | Propeller control, when mapped. |
| Brake input | Wheel braking. |
| Flaps input | Flap selection. |
| View controls | Switch between cockpit, exterior, instrument and replay cameras when available. |

Controller mappings are local user settings. If an axis is inverted or noisy, recalibrate it and verify the live input display before starting a scenario.

## Scenario Setup

The scenario selection screen provides approved pilot locations from CoreSim's default pilot scenario list. Choose a location and one of the supported start modes:

| Start mode | Behavior |
| --- | --- |
| Cold and dark | Aircraft starts parked with systems off where the scenario supports it. |
| Ready to taxi | Aircraft starts configured for taxi from an approved start point. |
| Airborne | Aircraft starts in flight at the selected scenario location. |

The scenario editor can save local user scenarios with aircraft ID, airport/runway or geographic position, heading, date/time, payload, fuel, weather and selected failures. Scenario files use schema `flying.user-scenario.v1`; unsupported or corrupt files are moved aside rather than loaded.

## Offline Map And Terrain Behavior

Flying is offline-first. Terrain, imagery, vector features, labels, world objects and the 2D navigation map are loaded from local package manifests and local archives. The runtime rejects package manifests that require external map APIs, remote tile servers, API keys or runtime network access.

The in-game offline navigation map displays local layers for airports, runways, obstacles, airspaces, labels, aircraft position, flight path and replay track. CUZK and package attribution is shown on the map and package manifests remain installed for inspection.

Terrain height and visual terrain are derived from approved local source packages. Package manifests record source lineage, CRS/geoid configuration, checksums, transformation metadata and validation reports. If a package is missing or fails validation, the affected terrain or map layer is unavailable until repaired.

## Replay

Telemetry and replay capture CoreSim inputs, states, forces, moments, configuration IDs, aircraft metadata, data versions and scenario metadata. Replays are intended for deterministic playback on compatible builds and data packages. Replay compatibility checks can refuse playback when the build, aircraft, scenario or data package identity does not match the recording policy.

Use replay cameras to inspect a completed flight. Replay evidence is not a certification record.

## Export

Post-flight export writes telemetry data for analysis and QA evidence. Exports are local files and should not contain personal telemetry by default. Before sharing exports, review the file for scenario names or operator notes that may identify a person or private location.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| Installer or executable signature warning | Stop and verify the release manifest and signature. |
| Missing terrain, GIS or navigation package | Run the installed offline repair tool and point it at the local package cache or removable media. |
| Offline map reports remote reference | Replace the package; release packages must not contain runtime map API or tile-server references. |
| Controller input reversed or drifting | Re-run calibration and inspect the live mapped input state. |
| Scenario file will not load | Check for `.corrupt` or `.unsupported` files beside the scenario; recreate the scenario if needed. |
| Replay refused | Use the same build and data package family that created the replay, or treat the recording as export-only evidence. |
| Crash or hang | Keep the local minidump, structured log and build metadata. Crash upload is disabled unless explicitly opted in. |

## Known Limitations

Flying is a consumer simulator and engineering validation target. It is not an EASA, FAA or UCL-certified flight training device and must not be used as an official navigation, training, procedure or aircraft qualification source.

Known release limitations are maintained in `docs/release/known-limitations.md`. The most important limitations are: aircraft fidelity is project-authored and unverified for named aircraft type behavior; fallback-derived airport, runway, SLZ, obstacle and airspace data can have uncertainty; CUZK-derived terrain and imagery are transformed and generalized; weather and failures are simulator models, not operational forecasts or maintenance diagnostics.

## Data Attribution

Flying packages that contain CUZK-derived data must show CUZK attribution in application notices, documentation, release notices and machine-readable package manifests. The canonical attribution text is recorded in `docs/governance/m0-legal-data-gate.md`; this manual summarizes the requirement and points users to the installed notices and package manifests.

AIM/AIP/VFR content is not included unless written permission and approved notices are archived. Operator-provided airport or runway notices must be displayed and documented for any package that uses those sources.
