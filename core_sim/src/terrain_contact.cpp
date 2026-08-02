#include "flying/core_sim/terrain_contact.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace flying::core_sim {
namespace {

void require_finite(double value, const char* field_name) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(std::string(field_name) + " must be finite");
  }
}

void validate_query(TerrainContactQuery query) {
  require_finite(query.latitude_deg, "latitude_deg");
  require_finite(query.longitude_deg, "longitude_deg");
  require_finite(query.aircraft_altitude_m, "aircraft_altitude_m");
  if (query.latitude_deg < -90.0 || query.latitude_deg > 90.0) {
    throw std::invalid_argument("latitude_deg must be in the [-90, 90] range");
  }
  if (query.longitude_deg < -180.0 || query.longitude_deg > 180.0) {
    throw std::invalid_argument("longitude_deg must be in the [-180, 180] range");
  }
}

[[nodiscard]] Vector3d ned_normal_from_enu(geo_terrain::Vector3d normal_enu) noexcept {
  return {normal_enu.y, normal_enu.x, -normal_enu.z};
}

} // namespace

TerrainContactData query_terrain_contact(const geo_terrain::ITerrainHeightService& terrain_service,
                                         TerrainContactQuery query) {
  validate_query(query);

  const geo_terrain::TerrainHeightSample sample =
      terrain_service.sample(geo_terrain::make_terrain_height_query_degrees(
          query.latitude_deg,
          query.longitude_deg));

  return {
    sample.height.meters,
    query.aircraft_altitude_m,
    query.aircraft_altitude_m - sample.height.meters,
    ned_normal_from_enu(sample.normal_enu),
    sample.surface_material,
    sample.collision.available,
    sample.collision.watertight,
    sample.collision.contact_offset_m,
    sample.collision.source_id,
    sample.source_tile_id,
    sample.confidence.normalized_confidence,
    sample.confidence.vertical_accuracy_m,
    sample.confidence.horizontal_accuracy_m,
    sample.source_authority,
    sample.runway_override_priority,
    sample.runway_override_active,
  };
}

TerrainContactData query_terrain_contact(const geo_terrain::ITerrainHeightService& terrain_service,
                                         const FlightDynamicsState& state) {
  return query_terrain_contact(terrain_service, TerrainContactQuery{
    state.latitude_deg,
    state.longitude_deg,
    state.altitude_m,
  });
}

} // namespace flying::core_sim
