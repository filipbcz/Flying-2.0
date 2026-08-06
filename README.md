# Flying

Offline-first Win64 flight simulator for the Czech Republic.

This repository currently contains the native C++ module boundaries, CMake presets, CI/local smoke validation, documentation, packaging and third-party notice areas. CoreSim now owns the deterministic fixed-step flight dynamics boundary, aircraft configuration ingestion and one unvalidated production aircraft data model, including an optional JSBSim-backed adapter for infrastructure testing. The data pipeline owns the GIS source-manifest foundation, DMR 5G pilot terrain packaging and pilot-region offline Ortofoto/ZABAGED/Geonames package processing; cockpit visuals, systems depth, final aircraft validation and release packaging are intentionally deferred to later roadmap steps.

See `docs/build-skeleton.md` for the current layout and validation entry point.
