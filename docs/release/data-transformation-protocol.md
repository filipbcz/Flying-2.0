# Flying Data Transformation Protocol

Document ID: REL-29-DATA-PROTOCOL  
Status: Release evidence candidate  
Scope: Release traceability for terrain, imagery, vector, airport, runway, obstacle, airspace and aircraft data

## Inputs

Every source package must be declared in a machine-readable source manifest before processing. The manifest must record:

| Field family | Requirement |
| --- | --- |
| Identity | Stable source ID, dataset name, provider and source package version or update date when available. |
| Provenance | Download URL or operator/source handoff record, acquisition date, reviewer and allowed-use decision. |
| License | License name, license URL where applicable, redistribution limits and attribution requirement. |
| Integrity | SHA-256 checksum and file size for every declared payload. |
| Geospatial metadata | CRS, vertical datum/geoid configuration, units, bounds and transformation parameters. |
| Use status | Allowed, blocked, derived, operator-confirmed or validation-pending status. |

## Approved Source Rules

CUZK DMR 5G, DMP 1G, Ortofoto Ceske republiky, ZABAGED and Geonames are approved only when sourced from official CUZK channels and recorded under CC BY 4.0 with attribution and change notices.

AIM/AIP/VFR content, AIM terrain datasets and AIM obstacle datasets are blocked for automated import, embedding and redistribution unless written permission explicitly covers the intended release use.

Airport, runway, SLZ, obstacle and airspace data may come from operator-provided confirmations, project-owned survey, manual derivation from approved CUZK sources, or permitted official registers used as completeness checklists. Derived values must remain labeled as derived until reviewed.

## Transformation Steps

1. Verify source files against declared checksums.
2. Validate provenance, license, attribution, CRS and permitted-use fields.
3. Transform source coordinates into the package CRS and local ENU frames with recorded PROJ/geoid configuration when applicable.
4. Generate terrain, imagery, vector, label, water, material, world-object, runway and navigation-map artifacts.
5. Run package-local validation: bounds coverage, tile hierarchy, control points, edge continuity, collision-tile availability, runway alignment and source-lineage completeness.
6. Emit deterministic package manifests with package ID, version, content hash, source lineage, runtime dependency declarations, generated file metadata and visible attribution flags.
7. Install package manifests with the product so users and QA can inspect license, attribution and transformation records offline.

## Output Traceability

Release terrain and GIS packages must include:

| Output | Required evidence |
| --- | --- |
| Terrain elevation | Source lineage, CUZK DMR 5G attribution, CRS/geoid configuration, control-point validation, edge-continuity validation and collision-tile metadata. |
| Imagery | Source lineage, CUZK Ortofoto attribution, mip/LOD metadata, checksums and change notice. |
| Vector map | Source lineage, CUZK ZABAGED and Geonames attribution, package bounds and layer inventory. |
| Airport/runway surfaces | Source type, operator or derivation record, uncertainty, runway dimensions/headings/slopes, Ortofoto alignment and terrain transition checks. |
| Obstacles/world objects | Source type, height policy, vector-property override status and derived-estimate label where applicable. |
| Navigation map | Local MBTiles/PMTiles archive, no remote tile-server URLs, visible attribution, required layer inventory and overlay metadata. |
| Aircraft data | Project-authored package notice, validation references, confidence bounds and no named-type fidelity claim. |

## Rejection Conditions

A package must not be released if it:

- requires runtime network access for normal flight;
- contains external map API keys or remote tile-server URLs;
- omits mandatory license or attribution metadata;
- includes AIM/AIP/VFR content without archived written permission;
- marks fallback-derived airport, runway, SLZ, obstacle or airspace data as validated without review;
- claims CUZK, AIM, operator, manufacturer or authority endorsement without written authorization;
- claims unsupported aircraft fidelity or flight-training certification.
