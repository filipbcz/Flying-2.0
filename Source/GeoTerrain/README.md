# GeoTerrain Source Boundary

`Source/GeoTerrain` is the roadmap-facing source tree for the GeoTerrain
module. The current native CMake implementation lives in `geo_terrain/` and
exposes its public API through `geo_terrain/include/flying/geo_terrain`.

GeoTerrain remains a standalone terrain and geodesy boundary. It may publish
public services to consumers, but must not depend on FlyingPresentation private
code.
