#include "flying/core_sim/terrain_contact.hpp"
#include "flying/geo_terrain/terrain_service.hpp"

#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace {

using flying::core_sim::FlightDynamicsState;
using flying::core_sim::TerrainContactData;
using flying::core_sim::Vector3d;
using flying::core_sim::query_terrain_contact;
using flying::geo_terrain::EnuVector;
using flying::geo_terrain::InMemoryDemSurface;
using flying::geo_terrain::InMemoryTerrainHeightService;
using flying::geo_terrain::OrthometricHeight;
using flying::geo_terrain::TerrainConfidenceMetadata;
using flying::geo_terrain::TerrainCollisionMetadata;
using flying::geo_terrain::TerrainLocalBounds;
using flying::geo_terrain::TerrainSourceAuthority;
using flying::geo_terrain::TerrainSurfaceMaterial;
using flying::geo_terrain::make_flat_terrain_plane;
using flying::geo_terrain::make_geodetic_degrees;

static_assert(std::is_same_v<decltype(TerrainContactData{}.surface_normal_ned), Vector3d>);
static_assert(std::is_same_v<decltype(TerrainContactData{}.terrain_elevation_m), double>);

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

void core_sim_queries_terrain_contact_without_presentation_types() {
  InMemoryTerrainHeightService terrain{make_geodetic_degrees(49.2, 16.6, {300.0})};
  terrain.add_dem_surface({
    TerrainLocalBounds{-100.0, 100.0, -100.0, 100.0},
    make_flat_terrain_plane(OrthometricHeight{325.0}),
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, true, 0.03, "core-contact-collision"},
    "core-contact-tile",
    TerrainConfidenceMetadata{0.92, 0.5, 1.0, true},
    20,
    "core-contact-grass",
  });

  const auto location = terrain.query_from_local_enu(EnuVector{30.0, -20.0, 0.0});
  const auto geodetic = flying::geo_terrain::GeodeticCoordinates{location.latitude_rad,
                                                                 location.longitude_rad,
                                                                 {0.0}};

  FlightDynamicsState state{};
  state.latitude_deg = geodetic.latitude_degrees();
  state.longitude_deg = geodetic.longitude_degrees();
  state.altitude_m = 350.25;

  const TerrainContactData contact = query_terrain_contact(terrain, state);

  require_near(contact.terrain_elevation_m, 325.0, 1.0e-9,
               "CoreSim contact query should return terrain elevation");
  require_near(contact.aircraft_altitude_m, 350.25, 0.0,
               "CoreSim contact query should echo aircraft altitude");
  require_near(contact.clearance_m, 25.25, 1.0e-9,
               "CoreSim contact query should return height above terrain");
  require_near(contact.surface_normal_ned.x, 0.0, 1.0e-12,
               "CoreSim contact normal north should be zero for flat terrain");
  require_near(contact.surface_normal_ned.y, 0.0, 1.0e-12,
               "CoreSim contact normal east should be zero for flat terrain");
  require_near(contact.surface_normal_ned.z, -1.0, 1.0e-12,
               "CoreSim contact normal should point up in NED coordinates");
  require(contact.surface_material == TerrainSurfaceMaterial::kGrass,
          "CoreSim contact query should return surface material");
  require(contact.collision_available, "CoreSim contact query should expose collision availability");
  require(contact.collision_watertight, "CoreSim contact query should expose collision continuity");
  require_near(contact.collision_contact_offset_m, 0.03, 0.0,
               "CoreSim contact query should expose collision offset");
  require(contact.collision_source_id == "core-contact-collision",
          "CoreSim contact query should expose collision source identity");
  require(contact.source_tile_id == "core-contact-tile",
          "CoreSim contact query should expose terrain source tile identity");
  require_near(contact.confidence, 0.92, 0.0,
               "CoreSim contact query should expose confidence score");
  require_near(contact.vertical_accuracy_m, 0.5, 0.0,
               "CoreSim contact query should expose vertical accuracy");
  require_near(contact.horizontal_accuracy_m, 1.0, 0.0,
               "CoreSim contact query should expose horizontal accuracy");
  require(contact.source_authority == TerrainSourceAuthority::kGenericDem,
          "CoreSim contact query should expose source authority");
  require(!contact.runway_override_active,
          "CoreSim contact query should expose inactive runway override state");
}

} // namespace

int main() {
  core_sim_queries_terrain_contact_without_presentation_types();
  return 0;
}
