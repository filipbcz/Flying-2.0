# M0 Aircraft Data Availability And Licensing Gate

Document ID: DR-M0-AIRCRAFT-DATA  
Version: 1.0.0  
Status: Blocked - reference aircraft not selected until licensing and validation evidence are confirmed  
Effective date: 2026-08-01  
Owner: Project governance  
Legal note: This is an engineering governance record, not legal advice.

## Decision

The Version 1 aircraft remains a generic requirement: one single-engine piston trainer aircraft. A real-world reference aircraft is not selected for Version 1 at M0.

Selection is blocked until the project archives written evidence that the chosen type's POH/AFM, aircraft identity permissions where needed, visual reference permissions where needed and flight-model validation data can be used for the intended simulator development and distribution.

No aircraft model may be marketed or documented as a faithful simulation of a specific aircraft type until it passes the validation package defined by the product requirements and the underlying data license permits that claim.

## Minimum Evidence For Selecting A Reference Aircraft

A candidate aircraft can be selected only after all required evidence is available:

| Evidence | Requirement |
| --- | --- |
| POH/AFM | Licensed or otherwise legally usable for internal modeling and validation. Redistribution of manual content is not allowed unless expressly licensed. |
| Aircraft name and marks | Written permission or legal clearance for using the aircraft make, model, logos, cockpit branding, trade dress and exterior livery. A generic unbranded trainer avoids this requirement but cannot claim type fidelity. |
| Geometry | Wing, tail, fuselage, control-surface, propeller and landing-gear dimensions with source provenance. |
| Mass properties | Empty mass, loading envelope, fuel stations, CG limits and moment/inertia basis with uncertainty notes. |
| Aerodynamics | Coefficients, derivatives, nonlinear stall/post-stall data or validated CFD/wind-tunnel/flight-test equivalents with permitted use. |
| Engine and propeller | Power, fuel flow, mixture, RPM, manifold pressure, temperature and propeller maps with permitted use. |
| Landing gear and brakes | Suspension, tire, friction, braking and ground-handling data with permitted use. |
| Validation references | Performance tables and test points for stall speeds, cruise, climb, glide, takeoff, landing, stability, control response and engine behavior. |
| Data provenance | Source, date, license, allowed uses, redistribution limits and confidence level for each data family. |

## Development Allowance Before Selection

Before the gate passes, future implementation steps may use an unbranded placeholder or community JSBSim model only for infrastructure development and test harness work. That placeholder:

- is not the selected Version 1 reference aircraft;
- must not ship as a faithful type-specific model;
- must not use restricted POH/AFM excerpts, proprietary aerodynamic data, trademarked branding or cockpit trade dress;
- must be labeled as approximate if used in internal demonstrations.

## Prohibited Claims

Until the gate passes, the project must not claim:

- fidelity to a named aircraft type;
- manufacturer endorsement;
- flight-training certification;
- validated stall, spin, performance or handling behavior for a named aircraft;
- exact cockpit, avionics or systems behavior for a named aircraft.

## Gate Exit Criteria

The aircraft data gate may move to Go only when:

- a specific reference aircraft is named in a new decision-record version;
- POH/AFM use rights are documented;
- validation data sources are documented and sufficient for the required acceptance tolerances;
- trademark, branding, livery and cockpit visual-reference risks are cleared or avoided through a generic unbranded aircraft;
- JSBSim baseline use and license obligations are recorded in the third-party license inventory;
- the aircraft's known limitations and unsupported regimes are documented before any release claim.

## Sources Checked

Sources were checked for this governance record on 2026-08-01:

- JSBSim repository and legal notice: https://github.com/JSBSim-Team/jsbsim
- JSBSim reference manual: https://jsbsim-team.github.io/jsbsim-reference-manual/
- Unreal Engine EULA: https://www.unrealengine.com/eula/unreal
