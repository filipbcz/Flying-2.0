# Canonical Flying-2.0 Editor baseline

The canonical project is `unreal/Flying.uproject` in
`https://github.com/filipbcz/Flying-2.0.git`. This is the project selected by
the existing production content-build manifest. The root presentation shell
is not the target of this acceptance run.

`preflight.json` records the leased checkout, base commit, host, Windows session,
UE patch/changelist, Cesium descriptor, MSVC, Windows SDK, GPU, interactive
desktop probe and available disk space. `build-result.json` records the
runner's actual structured Editor compilation, including the selected compiler
and SDK. `author-result.json` and `open-result.json` retain separate stdout,
stderr, exit codes, timestamps and structured invocation arguments.

The saved startup map is `/Game/Flying/Maps/FlyingStartup`. Its game-mode
Blueprint inherits the existing `FlyingPilotGameMode`; regional content still
comes from that mode's authorized runtime content builder. The material
`/Game/Flying/Materials/M_OfflineVertexColor` supplies the existing terrain
loader's vertex-color rendering contract, replacing an engine asset absent
from UE 5.8.3. The baseline adds no regional GIS package.

## Reproduction

1. In the authorized worker run
   `powershell -File Tools/UnrealBaseline/Record-Preflight.ps1`.
2. Use `run_unreal_authoring` with `tool=unreal-build`,
   `projectRelativePath=unreal/Flying.uproject`, `target=FlyingEditor`,
   `platform=Win64`, `configuration=Development`. Save its returned process
   record as `build-result.json`.
3. Use `run_unreal_authoring` with `tool=unreal-editor`, `phase=author` and
   `-ExecutePythonScript=<checkout>/Tools/UnrealBaseline/materialize_startup.py`.
   Save the returned process record as `author-result.json`.
4. Repeat in a separate process with `phase=author` and
   `-FlyingBaselineVerify`. Save its process record as `open-result.json`.
   This invocation only loads and inspects the serialized packages. The author profile
   retains D3D12 rendering; the runner adds NullRHI to its verify profile.
5. Run `node Tools/UnrealBaseline/validate_evidence.mjs --finalize --require-local-binaries`.
   The portable recheck is `node Tools/UnrealBaseline/validate_evidence.mjs`.

Pass a writable `-LocalDataCachePath=<checkout>/.ddc` in Editor calls. UE's
filesystem DDC path must be at most 119 characters; this lease's path is 104.
Editor executables and builds must always use the structured native tool.

`acceptance.json` binds the exact source/configuration inputs, engine-generated
packages, process records and build-product hashes to the preflight identity.
Binary hashes use raw bytes. Text hashes explicitly use UTF-8 with LF endings
so Git's Windows CRLF conversion does not invalidate equivalent source.
The standalone regression log records eight passing CoreSim/geodesy tests.

Run the native regression gate with
`cmake -P Tools/UnrealBaseline/validate_native.cmake`. It configures the existing
`test` preset into a separate `out/build/editor-baseline-native` directory with
a Makefile generator, builds the same eight CoreSim/geodesy targets and runs their CTest cases.
On Windows it imports the detected Visual Studio 2022 x64 compiler environment
and uses its bundled NMake. Other hosts use their normal C++ compiler and Make.
CMake 3.25+ is required.
Configuration, compilation or test failures return a nonzero exit code.

This replaces the three separate configure/build/CTest commands used in the
first attempt. The runner's validation reported MSBuild `GetOutOfDateItems`
failing to initialize `FileTracker` with `E_ACCESSDENIED` in `ZERO_CHECK.vcxproj`.
The same command subsequently passed through the authorized native process tool,
so the failure was not consistently reproducible there. NMake avoids that
MSBuild component while retaining header dependency tracking; no tests or
compiler checks are disabled. The general CMake presets and existing Visual
Studio build directory remain unchanged. `native-validation-correction.json`
records the clean and repeated NMake validation runs with separate stdout/stderr.

## Checkpoint and scope

`checkpoint.json` preserves completed steps. Missing preflight session/tools
produce `waiting_for_capability`; concrete build, script or load errors are
failures requiring repair, never capability waits. Do not transfer a checkpoint
to a different base commit or lease. Resume the first incomplete step with the
same artifacts. Finalization refuses missing/failed evidence, changed packages,
wrong identities or a wrong startup binding.

This gate establishes an Editor baseline. It does not certify Shipping, flight
performance, regional GIS completeness or the release-candidate gate.

## Observed upstream diagnostics

The installed Cesium 2.29.1 plugin reports an uninitialized reflected
`FCesiumMetadataPropertyStatisticValue::Semantic` property during Editor
initialization, accompanied by engine property-initialization check errors.
The complete diagnostics remain in `open-result.json` and `acceptance.json`.
The Editor exits successfully and the scoped startup/package checks pass.
This is not a claim that all engine/plugin automation tests pass. The plugin
diagnostic requires a separate upstream/toolchain resolution before a broader
release acceptance decision.

The runner denies Node child-process launch with EPERM. The four existing
production-inventory rejection fixtures were therefore run individually through
the native process tool; all rejected their intended invalid content. The new
baseline verifier's seven negative cases run in-process. A separate preflight
probe with an intentionally missing engine confirmed `waiting_for_capability`
preserves the completed-step checkpoint.
