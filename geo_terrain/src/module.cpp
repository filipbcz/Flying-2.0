#include "flying/geo_terrain/module.hpp"

namespace flying::geo_terrain {

ModuleBoundary describe_module() noexcept {
  return {
    "GeoTerrain",
    "Native boundary for future geodesy, terrain height and collision authority.",
    false,
  };
}

} // namespace flying::geo_terrain
