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

## Regional payload verification

The generated regional payload has a separate verifier from source intake.
For the existing managed publication checkpoint, run from the repository root:

```text
python Tools/data_pipeline/build_regional.py verify-staged --staging-root .forgemind-tmp/managed/regional-output-v3
```

On a fresh checkout, first materialize the published project-data directories
`data/sources`, `data/normalized`, `data/generated`, and `artifacts/regional-validation` at
the same relative paths beneath that staging root. Staged files remain outside
Git. Do not regenerate the payload just to verify it.

For a mounted persistent payload, use
`python Tools/data_pipeline/build_regional.py verify --data-root "<persistent-root>"`,
or set `FLYING_DATA_ROOT` explicitly. Bare `verify` requires that environment
variable; it does not discover runner-managed storage. Keep this requirement
so a missing configuration cannot silently select another dataset.

Use the Python environment containing NumPy, pyproj and rasterio. Native
authoring processes may redirect APPDATA and hide the probed Python user-site
packages; set PYTHONUSERBASE to that installation's existing user base for the
process when needed.

The default verifier is an acceptance gate: it exits nonzero for missing imagery
placement, missing independent survey controls, or comparable ground residuals
exceeding the declared dataset threshold.
`--integrity-only` explicitly checks checksums, collision and terrain topology;
it cannot establish acceptance. Generation saves a resumable checkpoint even
when unavailable upstream evidence blocks acceptance.

The v2 terrain NPZ format retains the sampled height lattice and adds
`mesh_vertices_east_north_baltic_m` (absolute S-JTSK east/north and Baltic 1957
height), `mesh_triangles`, `mesh_normal_enu`, and four `edge_*` index arrays.
Renderers must use the explicit mesh triangles. Every LOD retains every source
sample on its perimeter; boundary cells use triangle fans to connect that
perimeter to the coarse interior. Verification checks the actual triangle
boundary for holes and compares coordinates, heights and normals across every
adjacent LOD combination. Collision remains full-resolution DMR only.

Two upstream evidence files are supported alongside materialized sources:

- `ortofoto/export-placement.json`: `imageSha256`, `sourceUri`,
  `evidenceKind: "returned-export-response"`, returned `width`, `height`,
  and `extent` containing `xmin/ymin/xmax/ymax` and
  `spatialReference.wkid` (or `latestWkid`). The checksum and URI must match
  the acquired JPEG. Raw `ortofoto/export-response.json`, its managed receipt,
  `responseSha256`, `responsePath`, and returned `href` are retained and checked.
  Request bounds alone are insufficient because the export
  service can adjust the extent to match the image aspect ratio.
- `validation/height-controls.json`: `terrainSha256` matching the acquired
  DMR, `crs: "EPSG:5514+8357"`, `independentSurvey: true`,
  `provenance.sourceUri`, `provenance.surveyId`, documented
  `sourceUncertaintyM` and `uncertaintyProvenance`, and at least three
  non-collinear independently surveyed `points` with unique `id`,
  `eastM`, `northM`, and `baltic1957M`. Version 2 retains every returned
  `sourceAttributes` field, field definitions, control order, the literal mark
  type code from the survey URI, and an explicit comparability reason. Unknown
  physical mark types, per-point uncertainties and ground offsets remain null.
  Controls come from survey evidence, never from the terrain being checked.

## Bounded evidence acquisition and checkpoint recovery

`acquire_regional_evidence.py plan` emits only two managed HTTPS requests:
the configured orthophoto export with `f=pjson`, and a bounded envelope query
of CUZK BodovaPole MapServer layer 44 (ZVBP), returning native EPSG:5514 point
coordinates and Bpv `VYSKA`. The query uses the pilot polygon with zero extra
margin (within the configured 250 m allowance). It rejects truncated responses,
changed source requests, coordinate-axis conflicts, nonfinite heights and altered
managed receipts. Projected envelope-query fringe points outside the geographic
polygon are excluded by location only, with identities and reasons retained.
Raw responses retain every point, and every in-region point has a signed raw
DMR-minus-Bpv residual, including non-comparable marks. Reports group those
residuals by source control type, mark-type code and control order. Geographic
exclusions retain their complete source fields and reason; no control is
selected by its residual. Reference heights never come from DMR or DMP.

Run each returned request through `fetch_https`, then materialize the evidence
below `.forgemind-tmp/managed/evidence`. For the recovered checkpoint:

```text
python -B Tools/data_pipeline/acquire_regional_evidence.py stage --evidence-root .forgemind-tmp/managed/evidence --source-root .forgemind-tmp/managed/regional-output-v3/data/sources
python -B Tools/data_pipeline/acquire_regional_evidence.py verify --source-root .forgemind-tmp/managed/regional-output-v3/data/sources
```

An append-only `regional-evidence-manifest.json` extends the immutable source
acquisition manifest, binding its checksum and an `evidence` array containing
both raw responses and both derived records (SHA-256, kind, URI, retrieval).
The validation API merges this overlay only after verifying its base checksum;
the original acquired-source manifest and payloads remain byte-for-byte intact.
When an existing `data/sources` publication is immutable, the overlay and its
evidence remain in sibling `data/normalized` instead. The same API resolves
that explicit canonical sibling and still checks the immutable base checksum;
it does not overwrite another task's acquired-source collection.
Raw responses, receipts and derived records are also copied to
normalized storage and covered by checkpoint checksums. Export placement
preserves the actual service-adjusted extent, dimensions and temporary URL.
The historical JPEG is bound by checksum and identical request parameters;
the later metadata retrieval is explicitly recorded, not presented as a
new download of the temporary response image.

For controls, source terrain uncertainty is the documented DMR 5G complete
mean height error: 0.18 m in open terrain and 0.30 m in forest, as stated at
`https://ags.cuzk.gov.cz/arcgis2/rest/services/dmr5g/ImageServer`.
The dataset policy declares a 0.40 m maximum absolute ground residual (0.30 m
plus 0.10 m); this is not a claim that a mean error is a guaranteed pointwise
bound, nor that the service-resampled 2048-square export retains native-TIN
accuracy. That threshold applies only to controls with a documented ground
relationship, including a source URI, description and explicit
control-height-minus-ground-height offset (zero must also be documented).

The acquired layer-44 response provides no per-point absolute survey uncertainty,
physical mark description or mark-to-ground offset. Those fields remain null.
The returned survey URI's literal `TYP` code is retained without interpreting
it as a physical mark location. Every such control is classified
`not-comparable` with an explicit reason and excluded from ground-accuracy
acceptance, while its raw residual is retained in the independent plausibility
report. Missing per-point uncertainty does not fail acceptance. Large raw
residuals at non-comparable marks are not treated as bare-earth errors.

A plausibility pass with no comparable controls explicitly reports
`groundAccuracyStatus: not-assessed-no-comparable-controls` and a null maximum
ground residual; it does not certify absolute terrain accuracy. A comparable
control over the declared dataset threshold fails acceptance. No uncertainty,
offset or height is invented, and classification never depends on residuals.

After copying the augmented sources to a separate materialized input directory,
resume with `build_regional.py stage --managed-input <input> --staging-root
.forgemind-tmp/managed/regional-output-v3 --refresh-evidence`. This explicit,
reviewed v2 migration requires unchanged underlying source records, toolchain,
geometry schema and tiling. It rehashes retained payloads, archives the old
checkpoint, and invalidates only lineage, supplemental evidence, imagery and
reports, including the evidence overlay manifest. Terrain, collision, vectors,
masks and surface metadata are reused. Unchanged checksum-bound imagery is
also reused when only height-control evidence or its acceptance policy changes.
Unreviewed geometry processor changes still require a new output root.

`height-control-report.json` records all independent residuals, uncertainty
limitations and the decision separately from numerical transformation controls.
`seam-report.json` validates actual triangle boundaries and reports same-LOD
and mixed-LOD counts; keeping every source sample on each perimeter avoids
mixed-LOD T-junctions without regenerating valid geometry.

Publish all four directories through `publish_project_output` at exactly
`data/sources`, `data/normalized`, `data/generated`, and
`artifacts/regional-validation`. Publication preserves data and diagnostics;
it does not override failed height acceptance. Integrity-only verification
must never be reported as a release acceptance pass.
The unchanged source tree can be republished identically; new managed evidence
is durable in normalized storage and the runner-managed source-assets root.
