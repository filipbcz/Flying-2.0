# Flying

Offline-first Win64 flight simulator for the Czech Republic.

This repository currently contains the native C++ module boundaries, CMake presets, CI/local smoke validation, documentation, packaging and third-party notice areas. CoreSim now owns the deterministic fixed-step flight dynamics boundary, including an optional JSBSim-backed adapter for infrastructure testing. GIS processing, airport import, Unreal gameplay and real aircraft data are intentionally deferred to later roadmap steps.

See `docs/build-skeleton.md` for the current layout and validation entry point.
