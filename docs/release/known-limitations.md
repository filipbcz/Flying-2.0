# Flying Release Known Limitations

Document ID: REL-29-KNOWN-LIMITATIONS  
Status: Release evidence candidate  
Scope: Limitations that must be visible in release documentation and QA evidence

## Certification And Intended Use

Flying is not certified or approved as an EASA, FAA, UCL, FSTD, FNPT, flight-training, procedure-training, navigation or aeronautical information product. It must not be used as the sole basis for real-world flight planning, navigation, training credit, currency, aircraft checkout, emergency procedure qualification or operational decision making.

## Aircraft Fidelity

Flying Trainer One is an unbranded project-authored single-engine piston trainer data model. It is not a faithful simulation of a named aircraft type. No POH, AFM, manufacturer marks, cockpit trade dress or proprietary aerodynamic data are redistributed. Performance, stall, spin, engine, propeller, braking, ground handling, failure and systems behavior remain bounded by the validation evidence in `docs/validation/aircraft/flying_trainer_one/` and must not be marketed as manufacturer-grade or certification-grade fidelity.

## Terrain, Imagery And Map Data

CUZK-derived terrain, imagery and vector packages are transformed, tiled, generalized and adapted for simulator runtime use. They may differ from source data because of resampling, LOD generation, mesh simplification, visual material generation, coordinate transforms and package clipping. CUZK does not endorse or validate Flying.

Offline maps are simulator aids. They are not official aeronautical charts and can omit or simplify features.

## Airport, Runway, SLZ, Obstacle And Airspace Data

Fallback-derived airport, runway, SLZ, obstacle and airspace data can include manual measurements, project survey, operator confirmations and derivations from approved source data. Until each record has source, uncertainty and reviewer approval, it must remain unverified or derived. Derived obstacle heights and world-object placements are simulator estimates and must not be treated as official obstacle clearance data.

AIM/AIP/VFR import and redistribution are blocked unless written permission is archived. If permission is not archived for a release, the product must not claim to include authoritative AIM/AIP/VFR data.

## Weather And Atmosphere

Weather, turbulence, icing, precipitation, visibility, clouds and surface conditions are simulator models. They are not forecasts, observations, METAR/TAF replacements or operational weather products.

## Replay, Export And Diagnostics

Replay compatibility depends on build, aircraft, scenario and data-package identity. A replay refused by compatibility checks should be treated as export-only evidence. Telemetry exports and structured logs are QA artifacts, not certified flight records.

## Performance Evidence

Performance evidence is tied to the tested hardware, build configuration, terrain package and scenario. Passing the release performance gate on one reference PC does not certify performance for all hardware or package combinations.
