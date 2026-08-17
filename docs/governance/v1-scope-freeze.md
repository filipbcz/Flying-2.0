# Flying Version 1 Scope Freeze

Document ID: GOV-V1-SCOPE  
Version: 1.0.0  
Status: Frozen for Version 1 product planning; regional GIS packaging amendment active  
Effective date: 2026-08-01  
Owner: Project governance  
Scope type: Product requirements only; no implementation authority

## Authoritative Documentation Note

The current authoritative Flying 2.0 regional scope sources are `docs/contract/flying-2.0.yml`, this scope freeze, `docs/release/data-transformation-protocol.md` and `data_pipeline/README.md`. `AGENTS.md` is non-authoritative for this task if present; it may contain task-runner roadmap context, but it does not override the active regional GIS contract.

## Decision

Flying Version 1 is a Windows 64-bit desktop flight simulator for offline flight over the Czech Republic. The implementation baseline is:

- Target platform: Win64 on Windows 11 x64.
- Runtime and presentation engine: Unreal Engine 5.8 with C++.
- Geospatial runtime layer: Cesium for Unreal 2.28+.
- Simulation core: standalone C++ flight dynamics core running at a fixed 240 Hz step in double precision, with JSBSim as the baseline FDM library.
- Geographic coverage: Czech Republic product scope served by explicit regional package configurations. Complete-country data remains a possible configured package family, not the Raspberry Pi default data path.
- Primary aircraft content: one fully modeled single-engine piston trainer aircraft, selected only after the aircraft data gate confirms licensing and validation data.
- Runtime data posture: all required terrain, imagery, vector, map, airport and runway data for a selected flight region must be available from local versioned packages. GIS source and generated datasets are regional by default, live under configured data roots outside Git-tracked repository content, and are not complete-Czech-Republic downloads or local storage by default. Network access may be used only for explicitly requested data updates or optional live weather adapters.

This document freezes product scope for Version 1. It does not approve code implementation, importer implementation, data downloads, asset creation, or redistribution.

Older wording that implied mandatory whole-Czech-Republic GIS packaging for every target is superseded for Raspberry Pi constrained storage. The required implementation posture is regional, configurable GIS packages with manifest-backed provenance, licensing, checksums and runtime compatibility checks.

## Mandatory Version 1 Product Scope

Version 1 must deliver a complete flight workflow over configured Czech regions with one detailed piston trainer aircraft:

- uninterrupted flight inside the selected installed regional package without loading screens during normal flight;
- one physics-based single-engine piston trainer aircraft;
- 3D cockpit with all controls required for ordinary flight;
- cold-and-dark start, taxi, takeoff, flight, stall, spin tendency, landing, shutdown and emergency modes;
- real Earth position, latitude, longitude, altitude, time and solar position;
- wind, gusts, turbulence, pressure, temperature, air density, clouds and precipitation;
- terrain, collision, water, buildings and vegetation at distance-appropriate detail;
- all active runways for included Czech aerodromes and all SLZ areas accepted by the M0 airport data process;
- a custom offline 2D navigation map for the selected Czech region with no external map API dependency;
- keyboard, mouse, gamepad and common USB/HID flight-controller input;
- axis mapping, dead zones, response curves and multiple device profiles;
- scenario save, replay and telemetry export;
- graphics profiles including DLSS/TSR and object-density scaling;
- installer and signed Win64 Shipping build.

## Architecture Scope

Version 1 must preserve the following subsystem boundaries:

- CoreSim is a standalone C++ library independent of rendering frame rate and usable in headless tests.
- Geo/Terrain owns coordinate transforms, terrain height, surface normal, surface type and collision authority.
- Unreal Engine presentation displays authoritative simulation state and handles UI, cameras, audio and visual effects.
- The data pipeline runs outside the game and converts source GIS inputs into versioned terrain and map packages.
- Region configuration owns selected bounds, CRS metadata, margins and data roots. Ceska Trebova is a pilot configuration for a 10 x 10 km end-to-end proof, not hard-coded architecture logic.
- Telemetry and replay persist inputs, state, forces, moments, configuration and data versions sufficiently for reproducible playback.

Physics-critical logic belongs in C++. Blueprint is allowed for presentation logic, not equations of motion, aerodynamics, engine modeling, landing gear, braking or geodesy.

## Explicit Version 1 Exclusions

The following are out of Version 1 scope and must not be promised, planned as acceptance criteria, or treated as implied deliverables:

- EASA/FAA FSTD certification or any claim that the product is a certified training device;
- multiplayer;
- multi-player air traffic control;
- any territory outside the Czech Republic;
- transport-category aircraft;
- helicopters;
- combat systems;
- complete avionics suites for all manufacturers;
- VR, unless separately ordered and gated as a later scope item;
- force feedback, unless separately ordered and gated as a later scope item;
- photogrammetric 3D city coverage for the whole country.

## Scope Control Rules

- A feature is in Version 1 only if it is named in this scope freeze or in a later approved scope-change decision record.
- Future roadmap work must not be pulled into M0 documentation tasks.
- Data source use remains conditional on the legal/data gate.
- Aircraft identity and fidelity claims remain conditional on the aircraft data gate.
- "Realistic" means physics-based and validated against available licensed data. It does not mean certified for flight training.

## References

- Unreal Engine EULA: https://www.unrealengine.com/eula/unreal
- Cesium for Unreal repository and license notes: https://github.com/CesiumGS/cesium-unreal
- JSBSim repository and license notes: https://github.com/JSBSim-Team/jsbsim
- ČÚZK open data overview: https://geoportal.cuzk.cz/Default.aspx?mode=TextMeta&text=data_uvod
- AIM eAIP cover page: https://aim.rlp.cz/eaip/html/LK-cover-cz-CZ.html
- AIM VFR Manual: https://aim.rlp.cz/vfrmanual/actual/gen_1_cz.html
