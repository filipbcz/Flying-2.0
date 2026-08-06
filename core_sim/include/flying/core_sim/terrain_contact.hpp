#pragma once

#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/math.hpp"
#include "flying/core_sim/weather.hpp"
#include "flying/geo_terrain/terrain_service.hpp"

#include <string>

namespace flying::core_sim {

struct TerrainContactQuery {
  double latitude_deg{};
  double longitude_deg{};
  double aircraft_altitude_m{};
};

struct TerrainContactData {
  double terrain_elevation_m{};
  double aircraft_altitude_m{};
  double clearance_m{};
  Vector3d surface_normal_ned{0.0, 0.0, -1.0};
  geo_terrain::TerrainSurfaceMaterial surface_material{geo_terrain::TerrainSurfaceMaterial::kUnknown};
  bool collision_available{};
  bool collision_watertight{};
  double collision_contact_offset_m{};
  std::string collision_source_id;
  std::string source_tile_id;
  double confidence{};
  double vertical_accuracy_m{};
  double horizontal_accuracy_m{};
  geo_terrain::TerrainSourceAuthority source_authority{geo_terrain::TerrainSourceAuthority::kUnavailable};
  int runway_override_priority{geo_terrain::kNoRunwayOverridePriority};
  bool runway_override_active{};
  double weather_friction_scale{1.0};
};

[[nodiscard]] TerrainContactData query_terrain_contact(
    const geo_terrain::ITerrainHeightService& terrain_service,
    TerrainContactQuery query);

[[nodiscard]] TerrainContactData query_terrain_contact(
    const geo_terrain::ITerrainHeightService& terrain_service,
    const FlightDynamicsState& state);
[[nodiscard]] TerrainContactData apply_weather_to_terrain_contact(
    TerrainContactData contact,
    const WeatherSample& weather) noexcept;

} // namespace flying::core_sim
