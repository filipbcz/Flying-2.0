# Data Pipeline

Native C++ boundary for offline GIS and package-processing tools.

Current scope includes the pipeline foundation, DMR 5G pilot terrain processor,
pilot-region offline Ortofoto/ZABAGED/Geonames package processor and pilot
runway surface importer:

- JSON source manifests with provenance, license, attribution, checksum, CRS and permitted-use declarations.
- SHA-256 verification for declared source payloads.
- Validation reports that fail when mandatory source metadata is incomplete.
- Deterministic, versioned package manifests with source lineage.
- DMR 5G pilot-region terrain packaging from declared source tiles into a
  local project terrain package with recorded PROJ/geoid configuration,
  render LOD metadata, ENU normals, control-point validation, edge-continuity
  validation and separate physical collision tiles for the active aircraft zone.
- Pilot-region Ortofoto packaging from declared local source imagery into
  offline multi-level tiles with mipmaps.
- ZABAGED and Geonames conversion into local vector packages for roads, rail,
  water, settlements, vegetation areas, notable objects and labels.
- Water and material mask generation with package manifests that propagate
  attribution, source versions, checksums and license metadata while rejecting
  remote tile-server and map API references.
- Pilot runway import from approved airport seed records into per-aerodrome
  local ENU runway surface artifacts. The importer derives runway geometry from
  physical threshold coordinates, preserves longitudinal and transverse slope,
  emits visual and collision meshes with runway override priority, safe start
  positions, basic markings, taxi-connection stubs, LOD metadata and terrain
  transition bands, then writes a pilot coverage report with coordinate,
  heading, dimension, Ortofoto alignment, terrain transition and provenance
  checks.

The tool does not download source data, render runtime maps or depend on
public tile servers.
