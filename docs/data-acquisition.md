# Private pilot source acquisition

This intake is independent of Unreal and of the distributable release gate.
It does not declare a release candidate accepted. The configured rectangle in
`Config/Regions/ceska-trebova-pilot-region.json` is the acquisition polygon;
requests use zero additional margin (within the allowed 250 m). The acquisition
catalog is `Config/Acquisition/ceska-trebova.json`. Queries are restricted to its
HTTPS domains and exact geographic envelope. National official transformation
grids are the explicit supporting-data exception, not national terrain intake.

## Managed workflow

1. Set `FLYING_DATA_ROOT` to an absolute persistent directory **outside Git**.
   Logical `data/sources` and `artifacts/acquisition` paths resolve below it.
2. Run `python Tools/data_pipeline/acquire_sources.py plan`. The JSON queue
   contains the exact arguments for `forgemind_native.fetch_https`. Execute
   those requests with that managed tool, never a shell downloader. Every
   request requires 100,000,000,000 free bytes and has a maximum response size.
3. The managed transport owns partial files, HTTP Range/validator restart,
   checksums, public-address redirect guards, and provenance receipts. Rerun
   the same queue after interruption: destinations are stable, so the transport
   resumes partial downloads rather than creating alternate payloads. The plan
   skips only fully verified completed payload/receipt pairs already installed
   under the persistent root. Corrupt or absent pairs remain queued.
4. Materialize the managed `pilot` source collection below
   `.forgemind-tmp/managed/pilot`. Run:

   ```text
   python -B Tools/data_pipeline/acquire_sources.py stage --managed-input .forgemind-tmp/managed/pilot --staging-root .forgemind-tmp/managed/publication
   ```

   Staging is temporary, ignored Git data, **not persistent output**. The runner
   must publish `.forgemind-tmp/managed/publication/data/sources` with
   `publish_project_output` to exactly `data/sources`, and publish
   `.forgemind-tmp/managed/publication/artifacts/acquisition` to exactly
   `artifacts/acquisition`. Both publications are required. Existing outputs
   are never overwritten. On another machine, materialize these two managed
   project-data paths under a common temporary root before `verify-staged`.
5. With persistent storage mounted, `finalize --managed-input ...` installs the
   verified intake under `FLYING_DATA_ROOT`, and `verify` rehashes it. For the
   publication checkpoint use `verify-staged --staging-root ...` instead.

Only manifests, receipts and source payloads are published, never credentials.
Payload checksums are streamed, payload formats checked, service errors and
transfer-limit truncation rejected, and official grid hashes pinned. Copying
and manifest promotion are atomic per file; a partial installation never passes
verification. A repeat finalize repairs it from verified managed downloads.
Verification is read-only and does not require 100 GB free space; acquisition
planning and copying do. No network libraries or alternate download tools are
introduced.

## Source validation and limitations

`validate_sources.validate_source` remains the shared metadata/checksum API.
The new private intake explicitly disables only license/attribution requirements
for that call. Existing v1 distributable manifest validation retains its default
behavior and regressions. The private manifest has a separate schema identity
`flying.private-source-acquisition.v1`; it must not be passed off as a complete
distributable `flying.source-manifest.v1` or an approved airport database.

The collection includes real regional DMR 5G and DMP 1G float TIFF exports,
orthophoto JPEG, ZABAGED water/road/forest GeoJSON, Geonames settlements and
official CUZK horizontal/vertical grids from the PROJ CDN. Image exports are
2048 x 2048, not original point clouds or full native-resolution tile coverage.
Source-native coordinates and heights remain unchanged; `+proj=noop` describes
intake, not a completed production coordinate transformation. Grid availability
does not certify a transformation pipeline.

Official AIM public requests returned 404 and 403 on November 16, 2026. No access
controls were bypassed. Regional official ZABAGED airport and runway fallback
queries succeeded with zero features. Missing UCL/AIP/VFR/operator information
is recorded as a coverage limitation, not fabricated from empty responses, and
does not block independent GIS acquisition. Source versions use publisher grid
versions or content-addressed service snapshots. Retrieval dates are explicitly
not asserted to be survey dates or AIRAC effective dates. This workflow does
not require written redistribution approval and does not assert such approval.

## Regression checks

```text
python -B tools/data_pipeline/validate_sources.py --self-test
python -B -m unittest discover -s Tools/data_pipeline -p test_acquire_sources.py
python -B Tools/data_pipeline/acquire_sources.py verify-staged --staging-root .forgemind-tmp/managed/publication
```

The unittest suite also executes all existing source-validator self-tests. Its
workspace-scoped temporary fixtures inherit checkout permissions, avoiding the
Python 3.13 private temporary-directory ACL issue under a restricted Windows
runner token. It does not change production filesystem permissions.
