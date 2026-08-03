# Data Pipeline

Native C++ boundary for offline GIS and package-processing tools.

Current scope includes the pipeline foundation and the DMR 5G pilot terrain
processor:

- JSON source manifests with provenance, license, attribution, checksum, CRS and permitted-use declarations.
- SHA-256 verification for declared source payloads.
- Validation reports that fail when mandatory source metadata is incomplete.
- Deterministic, versioned package manifests with source lineage.
- DMR 5G pilot-region terrain packaging from declared source tiles into a
  local project terrain package with recorded PROJ/geoid configuration,
  render LOD metadata, ENU normals, control-point validation, edge-continuity
  validation and separate physical collision tiles for the active aircraft zone.

The tool does not download source data, tile ortofoto imagery, import runways
or render runtime maps.
