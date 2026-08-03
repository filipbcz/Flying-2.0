# M1 Known Limitations

Document ID: ENG-M1-KNOWN-LIMITATIONS  
Status: Active M1 limitation list  
Scope: Limitations that remain after the vertical-slice validation harness is present.

## Limitations

| Area | Limitation | Roadmap owner |
| --- | --- | --- |
| Aircraft fidelity | The M1 scripted flight uses the existing synthetic CoreSim rigid-body path and must be treated as unvalidated aircraft fidelity. Named-aircraft performance, stall, spin, handling and systems behavior are not complete. | Steps 16-20 |
| Terrain scope | Validation covers the pilot region and pilot runway fixtures only. It is not whole-country performance certification and does not certify full Czech Republic terrain continuity. | Step 21 |
| Airport scope | The runway checks cover the paved and grass pilot airports already imported for M1. Full airport, SLZ and detailed airport coverage remains out of scope. | Steps 22-23 |
| Unreal coverage | The native test proves deterministic CoreSim, terrain and telemetry contracts. The no-loading-screen result still needs an Unreal session on the reference PC for release evidence. | Step 30 |
| Performance evidence | The repository provides the M1 metric contract and deterministic headless budget fixture. Reference-class PC numbers must be captured from the manual Unreal run before marking the release gate complete. | Step 27 and Step 30 |
| Presentation detail | Cockpit instruments, detailed aircraft presentation, weather, world objects, vegetation, water, obstacles and audio are not validated by this M1 slice. | Steps 17-19 and Step 25 |

These limitations are intentional for M1 and must not be converted into pass status without the later roadmap work.
