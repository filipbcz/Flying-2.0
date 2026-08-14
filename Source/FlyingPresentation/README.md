# FlyingPresentation

Root Unreal Engine 5.8 runtime module for the Flying Win64 presentation shell.

The module owns the simulator startup scene binding, Cesium for Unreal georeference setup, and public presentation transforms between authoritative WGS-84 ECEF coordinates and Unreal coordinates. It may consume public simulation and terrain interfaces in later steps, but it must not include private CoreSim implementation headers.
