# CoreSim

Standalone native C++ boundary for the fixed-step flight dynamics core.

Current scope: a deterministic 240 Hz fixed-step kernel, double-precision authoritative state, SI-only internal unit policy, a minimal synthetic rigid-body integrator, a headless runner for native validation, and an optional JSBSim adapter with an infrastructure-only placeholder model. It intentionally contains no production aircraft data, terrain streaming, weather rendering, cockpit instruments, or replay file format.
