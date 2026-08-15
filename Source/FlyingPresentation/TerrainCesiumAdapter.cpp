#include "TerrainCesiumAdapter.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace flying::presentation {

namespace {

void require_finite(double value, const char* field_name) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(std::string(field_name) + " must be finite");
  }
}

void validate_query(CesiumTerrainQuery query) {
  require_finite(query.latitude_deg, "latitude_deg");
  require_finite(query.longitude_deg, "longitude_deg");
  require_finite(query.ellipsoidal_height_m, "ellipsoidal_height_m");
  if (query.latitude_deg < -90.0 || query.latitude_deg > 90.0) {
    throw std::invalid_argument("latitude_deg must be in the [-90, 90] range");
  }
  if (query.longitude_deg < -180.0 || query.longitude_deg > 180.0) {
    throw std::invalid_argument("longitude_deg must be in the [-180, 180] range");
  }
}

} // namespace

TerrainCesiumAdapter::TerrainCesiumAdapter(
    const geo_terrain::ITerrainHeightService& terrain_service) noexcept
    : terrain_service_(terrain_service) {}

CesiumTerrainSample TerrainCesiumAdapter::sample_for_visual_tile(CesiumTerrainQuery query) const {
  validate_query(query);
  const geo_terrain::TerrainHeightSample terrain = terrain_service_.sample(
      geo_terrain::make_terrain_height_query_degrees(query.latitude_deg, query.longitude_deg));

  return {
    query.latitude_deg,
    query.longitude_deg,
    terrain.height.meters,
    query.ellipsoidal_height_m - terrain.height.meters,
    terrain.normal_enu,
    terrain.surface_material,
    terrain.collision.available,
    terrain.collision.watertight,
    terrain.collision.contact_offset_m,
    terrain.collision.source_id,
    terrain.source_tile_id,
    terrain.source_authority,
  };
}

} // namespace flying::presentation
