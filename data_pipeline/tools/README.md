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

No downloader, importer, terrain transformer, airport processor or runtime GIS renderer is implemented in this foundation step.
