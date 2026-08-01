# M0 Go/No-Go Checklist

Document ID: CHK-M0-GO-NO-GO  
Version: 1.0.0  
Status: Active M0 checklist  
Effective date: 2026-08-01  
Owner: Project governance

## Gate Summary

M0 is the legal and data gate. It freezes Version 1 scope and determines whether later implementation work may import data, create assets, build aircraft physics models or ship packages.

Current gate posture on 2026-08-01:

- Scope freeze: Go for documentation.
- ČÚZK open-data use: Go for planning, conditional on attribution and manifest rules.
- AIM/AIP/VFR use: No-Go for automated import and redistribution until written permission or completed fallback evidence exists.
- Aircraft reference selection: No-Go until POH/AFM and validation data licensing are confirmed.
- Implementation work: No-Go in M0; this step is documentation-only.

## Required Checklist

| Item | Required evidence | Current status |
| --- | --- | --- |
| Version 1 scope is frozen | `docs/governance/v1-scope-freeze.md` names Win64, Unreal Engine 5.8, Cesium for Unreal 2.28+, offline Czech Republic coverage, one piston trainer aircraft and explicit exclusions. | Go |
| ČÚZK data sources are recorded | `docs/governance/m0-legal-data-gate.md` lists DMR 5G, DMP 1G, Ortofoto ČR, ZABAGED and Geonames. | Go |
| ČÚZK attribution is defined | English and Czech CC BY 4.0 attribution text is recorded and required for app credits, docs, release notices and package manifests. | Go |
| ČÚZK redistribution constraints are defined | Package manifests, change notices, no endorsement language and no extra restrictions on open-data components are recorded. | Go |
| AIM/AIP/VFR permission is resolved | Written permission from ŘLP ČR/AIM covering import, processing and redistribution is archived, or fallback evidence is approved. | No-Go |
| Airport master list process is approved | Active aerodromes, runways and SLZ areas can be enumerated from permitted sources with confidence and validation state. | No-Go |
| Aircraft reference is selected | A specific aircraft is named only after POH/AFM, visual identity and validation-data rights are confirmed. | No-Go |
| Aircraft validation data is available | Performance, aerodynamics, engine, propeller, mass, CG, inertia, gear and brake data are licensed or otherwise cleared. | No-Go |
| Scope exclusions are accepted | Certification, multiplayer, territory outside Czech Republic, transport aircraft, helicopters, combat systems, full avionics coverage, VR, force feedback and full-country photogrammetric cities are excluded from Version 1. | Go |
| Documentation-only boundary is preserved | No application source code, CoreSim implementation, importer, Unreal asset or runtime data package is changed by M0 documentation. | Go |

## M0 Pass Rule

M0 can pass only when every checklist item is Go. If any item is No-Go, the project may keep documentation and planning work but must not start dependent implementation work that assumes the blocked permission, source data or aircraft fidelity claim.

## Explicit Blockers

- No AIM/AIP/VFR automated import, extraction, embedding or redistribution until written permission or completed fallback evidence exists.
- No production airport, runway or SLZ manifest may mark restricted-source values as validated without permitted source evidence.
- No named-aircraft fidelity claim until the aircraft data gate passes.
- No simulator code, importer code, Unreal assets, CoreSim implementation or runtime data packages are part of M0.

## Next Allowed Actions After This Checklist

The next actions are governance-only until the blockers clear:

- request and archive written AIM/AIP/VFR permission, or approve the fallback data-acquisition plan;
- contact aerodrome and SLZ operators for written runway and surface confirmations;
- evaluate candidate single-engine piston trainer aircraft and secure data permissions;
- prepare later implementation tasks only after their upstream M0 gate dependencies are Go.
