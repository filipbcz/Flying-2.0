# CoreSim

Standalone native C++ boundary for the fixed-step flight dynamics core.

Current scope: a deterministic 240 Hz fixed-step kernel, double-precision authoritative state, SI-only internal unit policy, a minimal synthetic rigid-body integrator, and a headless runner for native validation. It intentionally contains no JSBSim integration, aircraft data, terrain streaming, weather rendering, cockpit instruments, or replay file format.
