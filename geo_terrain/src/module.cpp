#include "flying/geo_terrain/module.hpp"

namespace flying::geo_terrain {

ModuleBoundary describe_module() noexcept {
  return {
    "GeoTerrain",
    "Shared WGS-84 geodesy, local frames, height semantics and future terrain authority.",
    false,
  };
}

} // namespace flying::geo_terrain
