# M1 Vertical Slice Flight Plan

Document ID: ENG-M1-VSLICE-FLIGHT-PLAN  
Status: Validation procedure  
Scope: Pilot-region loading, takeoff, circuit, landing, replay and telemetry evidence.

## Automated Scenario

The native M1 test `flying.m1.vertical_slice` runs the headless scripted route below against the existing CoreSim telemetry and replay stack. It uses the FPPV runway 09 pilot start, in-memory pilot terrain/runway fixtures, fixed 60 Hz caller frames and the 240 Hz CoreSim fixed step.

| Phase | Route intent | Automated pass condition |
| --- | --- | --- |
| Taxi | Ready-to-taxi start on FPPV runway 09. | Terrain is loaded, runway override is active, no loading-screen marker is emitted. |
| Takeoff roll | Accelerate on the paved runway. | Runway override remains active and telemetry records every frame. |
| Climb out | Leave the runway into the pilot region. | Aircraft state becomes airborne and terrain samples stay inside the loaded pilot package. |
| Crosswind | Turn away from runway heading. | Circuit phase is present in telemetry and terrain continuity holds. |
| Downwind | Fly opposite the active runway. | Pilot-region terrain remains loaded without route gaps. |
| Base | Descend toward final. | Terrain collision metadata remains available. |
| Final | Align with runway 09. | Descent profile returns toward runway height. |
| Landing roll | Touch down and decelerate on FPPV. | Runway override is active and telemetry remains continuous. |
| Stop | Stop on the runway and end recording. | Final forward speed is below taxi speed and replay reproduces the final state hash. |

The scripted scenario is an integration validation for M1 plumbing. It does not validate a named aircraft's aerodynamic fidelity.

## Manual Unreal Procedure

1. Generate or provide the local pilot terrain, pilot GIS and pilot runway surface package manifests at the paths configured in `unreal/Config/DefaultGame.ini`.
2. Launch the Unreal 5.8 project and start `FPPV-RWY-09` in `ready_to_taxi` mode.
3. Start telemetry recording before releasing brakes.
4. Taxi, take off, fly one left-hand circuit, land on FPPV runway 09, stop on the runway and save telemetry.
5. Confirm the flight never presents a loading screen or runtime map dependency prompt after scenario start.
6. Load the saved telemetry as a replay, play it, and verify replay reports deterministic state hashes or documented compatibility warnings only.
7. Export telemetry CSV or JSON for the M1 validation report.

Manual evidence should include the telemetry file path, replay status, package manifest versions, and any limitation that prevents a pass.
