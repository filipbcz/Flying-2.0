# CoreSim

Standalone native C++ boundary for the fixed-step flight dynamics core.

Current scope: a deterministic 240 Hz fixed-step kernel, double-precision authoritative state, SI-only internal unit policy, a minimal synthetic rigid-body integrator, a headless runner for native validation, an optional JSBSim adapter with an infrastructure-only placeholder model, and one unvalidated production aircraft data model under `core_sim/aircraft`. It intentionally contains no terrain streaming, weather rendering, cockpit instruments, or replay file format.
