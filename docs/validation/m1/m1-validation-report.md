# M1 Vertical Slice Validation Report

Document ID: ENG-M1-VSLICE-REPORT  
Status: M1 validation harness ready; reference-PC Unreal evidence required for final release-gate sign-off  
Date: 2026-08-03

## Pass/Fail Summary

| Area | Status | Evidence |
| --- | --- | --- |
| Automated M1 suite | Pass when `tools/validate_m1_vertical_slice.sh` exits 0 | Runs CoreSim unit coverage, geodesy, terrain service, runway importer, replay/telemetry hash coverage, pilot package validation and `flying.m1.vertical_slice`. |
| Pilot-region loading | Pass for native/package contracts | Pilot package, runway importer and M1 terrain-continuity checks keep runtime network and remote tile dependencies out of the slice. |
| Scripted takeoff-circuit-landing | Pass for headless M1 integration | `flying.m1.vertical_slice` taxis, rolls, climbs, flies circuit phases, lands, stops, records telemetry and replays deterministic state hashes. |
| Terrain continuity | Pass for M1 route fixture | Adjacent pilot terrain seam samples are height-continuous and collision metadata remains available along the scripted route. |
| Replay and export readiness | Pass for native telemetry/replay contracts | The M1 suite includes telemetry round-trip, replay compatibility and replay hash checks. |
| CoreSim timing | Pass for fixed-step budget fixture | The scripted 60 Hz caller stream expects four 240 Hz CoreSim fixed steps per frame and reports zero step misses. |
| Performance capture | Pass for metric contract; manual reference-PC capture still required | `performance-capture-template.json` defines FPS, 1% low, CoreSim step misses, input latency, hitch count, RAM and VRAM fields and budgets. |
| Aircraft fidelity | Not complete | M1 does not validate a named aircraft data model, aerodynamic performance, systems, sensors, cockpit or final aircraft handling. |

## Validation Entry Points

Run the M1 automated suite from the repository root:

```sh
tools/validate_m1_vertical_slice.sh
```

For CTest-only execution after configuring and building the required targets:

```sh
ctest --preset m1-vertical-slice --output-on-failure
```

## Required Manual Evidence

The Unreal reference-PC run should attach these records before release-gate sign-off:

| Evidence | Required fields |
| --- | --- |
| Flight telemetry | Scenario id, start mode, package versions, full taxi/takeoff/circuit/landing/stop recording, replay result. |
| No-loading-screen observation | Start time, route flown, pilot packages loaded, confirmation that no loading screen or runtime map dependency appears after scenario start. |
| Performance capture | Average FPS, 1% low FPS, CoreSim step misses, p95 input latency, hitch count over 50 ms, RAM peak and VRAM peak. |
| Known limitations | Any failed, skipped or manually deferred item linked to `known-limitations.md`. |

## Known Limitations

See `docs/validation/m1/known-limitations.md`. The key release note is that unvalidated aircraft fidelity remains explicitly out of scope for M1 and is not marked complete by this report.
