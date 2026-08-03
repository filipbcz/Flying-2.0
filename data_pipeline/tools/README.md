# Data Pipeline Tools

`flying-data-pipeline` provides the offline GIS package foundation.

Validate a source manifest and write a machine-readable validation report:

```sh
flying-data-pipeline validate \
  --source-manifest path/to/source-manifest.json \
  --source-root path/to/source-payloads \
  --report out/validation-report.json
```

Generate a deterministic package manifest after validation succeeds:

```sh
flying-data-pipeline package \
  --source-manifest path/to/source-manifest.json \
  --source-root path/to/source-payloads \
  --package-name pilot-terrain \
  --package-version 2026.08.0 \
  --output out/package-manifest.json \
  --report out/validation-report.json
```

The command exits non-zero when required provenance, checksum, CRS, license,
attribution or permitted-use metadata is missing or when checksum verification
fails.

Generate a DMR 5G pilot-region terrain package after the same source manifest
gate succeeds:

```sh
flying-data-pipeline dmr5g-pilot-terrain \
  --source-manifest path/to/source-manifest.json \
  --source-root path/to/source-payloads \
  --terrain-config path/to/dmr5g-pilot-terrain-config.json \
  --package-version 2026.08.0 \
  --output-dir out/dmr5g-pilot-terrain \
  --report out/dmr5g-pilot-terrain-validation.json
```

The terrain config declares the 50 x 50 km pilot bounds, DMR 5G source tile
paths, recorded PROJ/geoid setup, render LOD strides, active aircraft collision
zone and control points. The output package contains CSV render LOD tiles with
ENU normals plus separate collision CSV tiles.

No downloader or runtime GIS renderer is implemented in this step.

Import the two pilot airport runway surfaces from the approved airport seed:

```sh
flying-data-pipeline runway-import \
  --airport-database data_pipeline/seeds/pilot-airport-master-list.json \
  --package-version 2026.08.0 \
  --output-dir out/pilot-runway-surfaces \
  --report out/pilot-runway-coverage-report.json
```

The runway importer rejects runway geometry that cannot be built from physical
threshold coordinates. Generated artifacts include sloped visual and collision
meshes, runway override priority metadata, safe start positions, material
mapping, basic markings, taxi-connection stubs, LOD declarations and terrain
transition smoothing bands.
