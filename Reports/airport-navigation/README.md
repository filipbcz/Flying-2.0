# Bounded airport and offline navigation package

Replaces the historical network-disabled blocker checkpoint. This is a private,
non-distributed simulator package, not RC acceptance or an operational chart.
The configured pilot bounds and 250 m discovery margin are unchanged.

## Payload and discovery

- LKCTRE: active operator-published **Plocha SLZ neověřená**, retained unverified.
- RWY 15/33: published 350 x 20 m; the visual and contact meshes now use the
  full published dimensions on an Ortofoto-derived axis with DMR heights.
  This fixes the 10 m width substitution, not the unknown physical threshold accuracy.
- One opt-in-only static contact-mesh safe start; no aircraft clearance approval.
- LKUO, LKVM, LKMK and LKCHOC: source ARPs reviewed, outside the pilot margin;
  source text and calculated distances are retained in `project-catalogue.json`.
- Remaining national index entries are explicitly not spatially reviewed, not
  falsely classified as outside the polygon. No national completeness claim.
- Nine offline source GeoJSON layers include the real SLZ, derived runway, roads, forest,
  water, settlements, power infrastructure and advisory nominal LKUO ATZ.
  Local AIM chapters/maps accompany them; airspace is not operationally complete.
- `navigation/navigation.mbtiles` contains 1,027 actual PNG raster tiles at zooms
  12 through 16, with Web Mercator / TMS addressing, clipped to the unchanged
  pilot bounds. All nine source layers are rendered; `navigation/style.json`
  is a local-only raster style. The manifest binds both by SHA-256 and includes
  layer counts and a color legend. A compatible viewer must resolve the local
  `mbtiles://navigation.mbtiles` URI; this is not an external tile endpoint.
  PNGs are a private simulator overview, not labelled operational charts.

## Durable installation layout

Runner-managed project data (not Git) contains:

- `data/sources`: original checksum-bound CUZK acquisition, preserved unchanged.
- `data/packages/regional-airport-navigation/sources/airport-navigation`: additional
  AIM/operator snapshots, bounded Ortofoto/DMR evidence and acquisition sidecars.
  These also remain in runner-managed source-assets `airport-navigation`.
- `data/packages/ceska-trebova-airport-navigation-2026-11-19-private-3.zip`:
  self-contained regional archive including terrain, transformation grids,
  navigation, runway/contact mesh, start and additional source provenance.
- `artifacts/airport-navigation`: current coverage, geometry, provenance,
  checksum, relocation and acceptance reports mirrored here.

The existing source root belongs to the acquisition task and rejects changed
content; no original dataset was overwritten. Additional sources are therefore
embedded in the installable package with working relative provenance paths.

Checksums and file inventory validate before and after extraction to an unrelated
path with spaces. Run `python Tools/data_pipeline/test_build_airport_navigation.py`
for focused negative tests, or `python Tools/data_pipeline/build_airport_navigation.py
verify --root <installed-package>` to validate an installed copy. GIS Python requires
numpy, rasterio, pyproj and beautifulsoup4. On this leased runner APPDATA is isolated;
set PYTHONUSERBASE to the probed Python user installation when executing these tools.

No runtime external tiles, Unreal changes, two-airport flight, regulator verification,
redistribution permission or release-candidate approval are asserted. Geometry
uncertainty allowances are reported as engineering estimates, not measured accuracy.
Unmeasured terrain transitions are omitted rather than labelled validated.

## Private package acceptance and production precision

The available sources contain no independent surveyed threshold/edge control
demonstrating 1 m threshold, 0.1 degree heading and 0.5 m dimension accuracy.
The 5 m horizontal, 1 degree heading and 25 m threshold station allowances are
engineering estimates, not measured errors. Imagery centerline residuals and
pixel size must not be substituted for independent absolute accuracy.

`geometry-report.json` records null measured errors and independent control;
`production-gate-report.json` preserves the blocked production diagnostic.
`acceptance-report.json` records the separate private technical gate result.
`build_airport_navigation.py production-gate --root <installed-package>` exits
nonzero until independent measured control satisfies all three limits. Package
integrity and relocation passing do not waive production certification or validate the SLZ.
These survey thresholds are not acceptance requirements for this private pilot task.

To reproduce this correction without repeating GIS acquisition, materialize the
previous regional payload under `<output>/data/packages/regional-airport-navigation`
and its reports under `<output>/artifacts/airport-navigation`, then run
`python Tools/data_pipeline/build_airport_navigation.py refresh --output <output>`.
The relocation destination must be fresh; stale extracted data is not reused.

## Authoritative private-package command

Materialize managed `data/sources`, `data/packages` and
`artifacts/airport-navigation` under `.forgemind-tmp/managed/private-acceptance/`
with their relative paths preserved. Run the directly installed interpreter:

```powershell
$env:PYTHONUSERBASE='C:/Users/borek-fil/AppData/Roaming/Python'
& 'C:/Users/borek-fil/AppData/Local/Programs/Python/Python313/python.exe' Tools/data_pipeline/build_airport_navigation.py private-package-gate --output .forgemind-tmp/managed/private-acceptance
exit $LASTEXITCODE
```

This command exits nonzero for missing or altered real-source provenance, incomplete
or changed derived geometry, unsafe/default-enabled starts, missing local tiles/style,
coverage gaps, checksum mismatch, or failed fresh archive relocation. It checks all
31 acquisition records, reproduces the Ortofoto/DMR geometry through the existing
builder, validates 1,027 PNG tiles across nine layers, and preserves the operator's
unverified status. No downloads or runtime network are used.

The current package passes this gate with one derived-unverified-private 350 x 20 m
runway and one opt-in static contact start. The existing production-gate command
continues to reject the absent independent survey measurements. That expected
diagnostic is not the authoritative acceptance command for this task.
