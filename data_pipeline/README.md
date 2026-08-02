# Data Pipeline

Native C++ boundary for offline GIS and package-processing tools.

Current scope is the pipeline foundation only:

- JSON source manifests with provenance, license, attribution, checksum, CRS and permitted-use declarations.
- SHA-256 verification for declared source payloads.
- Validation reports that fail when mandatory source metadata is incomplete.
- Deterministic, versioned package manifests with source lineage.

The tool does not download source data, convert DMR 5G terrain, tile imagery, import runways or render runtime maps.
