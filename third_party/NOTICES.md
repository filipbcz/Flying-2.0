# Third-Party Notices

This repository skeleton does not vendor third-party code, binary artifacts or source data.

Future releases must include notices and license obligations for Unreal Engine, Cesium for Unreal, JSBSim, PROJ, the selected test framework, packaging tools, signing tools and any redistributed data packages.

## JSBSim

- Intended integration version: JSBSim 1.2.3, tag `v1.2.3`, commit `570e8115a102df8f877b11e0e59b964ea483e3c0`.
- License: LGPL-2.1-or-later per the upstream JSBSim project.
- Repository status: not vendored. CoreSim builds the JSBSim adapter when installed development files are present, or when the explicit `FLYING_CORE_SIM_FETCH_JSBSIM` CMake option is enabled.
- Local placeholder model: `core_sim/jsbsim/aircraft/flying_placeholder` is project-authored infrastructure test data only and does not claim fidelity to any named aircraft.
