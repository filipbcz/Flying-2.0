# Installed regional scenario packages

The sole selection authority is CoreSim's
`SelectedPackageRuntime` in `core_sim/include/flying/core_sim/selected_package.hpp`.
Select a configured data root, relative installed-package directory and installer-trusted
SHA-256 of `install-manifest.json`. No package is selected by default. Do not derive
the trusted hash from a potentially modified installed manifest in production; the
integration harness computes it only for its runner-managed input.

`current()` publishes validated package identity/version/hash, canonical data root and
package location, geographic CRS/bounds, metadata-derived origin, generated safe-start
records and a generation token. The airport metadata reference point defines the package
origin; local runway meshes can retain their own per-airport origins. Consumers must
retain the generation and invalidate cached state when it changes. Successful and failed
selection attempts, and clear, invalidate prior selections. Selection and starts validate
all manifest members; paths resolving outside the root/package are rejected.

Unreal callers use `UFlyingInstalledPackageLibrary::SelectInstalledPackage` once and
both widgets enumerate `GetPackageLocations`. This is a bridge to the same CoreSim
instance, not a second selection service. Saved scenarios record the package manifest
hash; loading requires the same validated package and explicitly rebinds the generation.
Old fixture-only scenario saves must be rebound to installed package identities.

Ground starts require generated eligibility, active/verified airport metadata,
production-validated and manually verified runway metadata, checksum-bound provenance
and an installed matching contact surface. Unverified fields remain visible with reasons;
they are not silently promoted to eligible starts. Airborne catalog starts retain the
generated start altitude (safe-start elevation plus 450 m), not ground operational approval.
Both catalog and arbitrary geographic airborne starts must be inside bounds, above a
successful terrain sample by more than 1.5 m and use an available aircraft. The actual
sample supplies the simulator terrain elevation; safe-start metadata is not a terrain
coverage fallback. A terrain consumer registers
`set_terrain_elevation_provider`; it receives the complete selected configuration on
each query and must return no value for uncovered terrain. Missing providers/data fail
closed. This step does not implement the dependent world renderer.

Fixed pilot origin and Saved/Flying/PilotRegion path defaults are removed. Deprecated
presentation settings remain empty/zero for serialized asset compatibility; world
consumers must migrate to the shared selected configuration, not establish selection.

## Focused verification

Run `cmake -P Tools/ScenarioPackageIntegration/validate_native.cmake` for the
focused configure/build/test gate. On Windows it initializes the installed x64
MSVC environment and uses NMake rather than relying on the caller's PATH or
MSBuild FileTracker. It uses a separate build cache so a failed Visual Studio
configuration cannot poison retries. On other hosts it uses Unix Makefiles.
This contract-only gate disables JSBSim; it is not physics or Shipping acceptance.

Build `flying_core_sim_scenario_start_tests` with CMake and run
`ctest --test-dir <build> -C Debug -R ^flying.core_sim.scenario_start$ --output-on-failure`.
Its default mode creates temporary contract-only packages, never runtime fallback data.
Its `--production <data-root> <relative-package> <trusted-manifest-sha256>` mode
checks the actual installed package without altering operational verification status,
including rejection of catalog airborne starts when no terrain provider is registered.

Run `Tools/ScenarioPackageIntegration/preflight.mjs` with the evidence directory,
runner-probed engine root and Cesium manifest path before Editor integration. It verifies
the satisfied canonical startup assets without recreating or changing that baseline.
Run `verify_widgets.py` through the structured Unreal Python authoring adapter after
compiling the Editor target. Evidence is written under
`.forgemind-tmp/scenario-package-integration` and published to project data at
`artifacts/scenario-package-integration`. This is not final release acceptance.
