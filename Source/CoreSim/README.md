# CoreSim Source Boundary

`Source/CoreSim` is the roadmap-facing source tree for the CoreSim module.
The current native CMake implementation lives in `core_sim/` and exposes its
public API through `core_sim/include/flying/core_sim`.

CoreSim remains a standalone native library. It must not depend on Unreal,
Cesium, FlyingPresentation, or Blueprint assets.
