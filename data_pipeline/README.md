# Data Pipeline

Native C++ boundary for offline GIS and package-processing tools.

The authoritative Flying 2.0 data posture is regional by default. GIS source
discovery, normalization and generated runtime packages are driven by explicit
region configuration and configured margins, with source and generated datasets
stored under configured data roots outside Git-tracked repository content.
`AGENTS.md` is non-authoritative for this task if present.

Current scope includes the pipeline foundation, DMR 5G terrain processor,
offline Ortofoto/ZABAGED/Geonames package processor and pilot runway surface
importer:

- JSON source manifests with provenance, license, attribution, checksum, CRS and permitted-use declarations.
- SHA-256 verification for declared source payloads.
- Validation reports that fail when mandatory source metadata is incomplete.
- Deterministic, versioned package manifests with source lineage.
- DMR 5G terrain packaging from declared CUZK source tiles into a local project
  terrain package with recorded PROJ/geoid configuration, render LOD metadata,
  ENU normals, control-point validation, edge-continuity validation and
  separate physical collision tiles. Configs target explicit regions; whole
  Czech Republic coverage is a separately approved package mode, not the
  default path for Raspberry Pi constrained storage.
- Ortofoto packaging from declared local source imagery into offline
  multi-level tiles with mipmaps for configured regional coverage.
- ZABAGED and Geonames conversion into local vector packages for roads, rail,
  water, settlements, vegetation areas, notable objects and labels.
- Water and material mask generation with package manifests that propagate
  attribution, source versions, checksums and license metadata while rejecting
  remote tile-server and map API references.
- Regional terrain and offline GIS manifests include bounds metadata,
  uninterrupted local streaming metadata within the selected region, generated
  package byte counts, tile hierarchy counts and collision-tile availability
  for later release packaging and performance testing.
- Ceska Trebova is the first 10 x 10 km pilot region configuration used to
  prove the end-to-end process. It must remain configuration data and must not
  become a hard-coded assumption in pipeline logic.
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
