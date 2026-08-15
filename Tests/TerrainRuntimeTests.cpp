#include "TerrainHeightService.h"
#include "TerrainCesiumAdapter.h"
#include "flying/core_sim/terrain_contact.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {

using flying::core_sim::TerrainContactQuery;
using flying::core_sim::query_terrain_contact;
using flying::geo_terrain::EnuVector;
using flying::geo_terrain::InMemoryDemSurface;
using flying::geo_terrain::InMemoryTerrainHeightService;
using flying::geo_terrain::OrthometricHeight;
using flying::geo_terrain::TerrainConfidenceMetadata;
using flying::geo_terrain::TerrainCollisionMetadata;
using flying::geo_terrain::TerrainHeightQuery;
using flying::geo_terrain::TerrainHeightSample;
using flying::geo_terrain::TerrainLocalBounds;
using flying::geo_terrain::TerrainSourceAuthority;
using flying::geo_terrain::TerrainSurfaceMaterial;
using flying::geo_terrain::make_geodetic_degrees;
using flying::geo_terrain::make_sloped_terrain_plane;
using flying::presentation::CesiumTerrainQuery;
using flying::presentation::TerrainCesiumAdapter;

static_assert(std::is_abstract_v<flying::geo_terrain::ITerrainHeightService>);

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(std::string(message) + ": expected " + std::to_string(expected) +
                             ", got " + std::to_string(actual));
  }
}

[[nodiscard]] InMemoryTerrainHeightService make_active_slice_service() {
  InMemoryTerrainHeightService service{make_geodetic_degrees(49.20, 16.60, {300.0})};
  service.add_dem_surface({
    TerrainLocalBounds{-600.0, 25.0, -350.0, 350.0},
    make_sloped_terrain_plane(OrthometricHeight{305.0}, 0.0, 0.0, 0.008, -0.003),
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, true, 0.04, "dmr5g-west-collision-lod0"},
    "dmr5g-west-lod0",
    TerrainConfidenceMetadata{0.97, 0.18, 0.50, true},
    100,
    "active-slice-west",
  });
  service.add_dem_surface({
    TerrainLocalBounds{-25.0, 600.0, -350.0, 350.0},
    make_sloped_terrain_plane(OrthometricHeight{305.0}, 0.0, 0.0, 0.008, -0.003),
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, true, 0.04, "dmr5g-east-collision-lod0"},
    "dmr5g-east-lod0",
    TerrainConfidenceMetadata{0.97, 0.18, 0.50, true},
    100,
    "active-slice-east",
  });
  service.add_dem_surface({
    TerrainLocalBounds{-600.0, 600.0, -350.0, 350.0},
    make_sloped_terrain_plane(OrthometricHeight{304.95}, 0.0, 0.0, 0.0081, -0.0031),
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, true, 0.05, "dmr5g-lod1-collision"},
    "dmr5g-lod1",
    TerrainConfidenceMetadata{0.94, 0.30, 1.00, true},
    50,
    "active-slice-lod1",
  });
  return service;
}

[[nodiscard]] TerrainHeightSample sample_local(const InMemoryTerrainHeightService& service,
                                               EnuVector local) {
  const TerrainHeightQuery query = service.query_from_local_enu(local);
  return service.sample(query);
}

void coresim_and_presentation_share_terrain_height_service_api() {
  auto service = make_active_slice_service();
  const auto query = service.query_from_local_enu(EnuVector{120.0, -40.0, 0.0});
  const auto geodetic = flying::geo_terrain::GeodeticCoordinates{
    query.latitude_rad,
    query.longitude_rad,
    {0.0},
  };

  const auto core_sample = query_terrain_contact(service, TerrainContactQuery{
    geodetic.latitude_degrees(),
    geodetic.longitude_degrees(),
    340.0,
  });
  const TerrainCesiumAdapter presentation_adapter{service};
  const auto visual_sample = presentation_adapter.sample_for_visual_tile(CesiumTerrainQuery{
    geodetic.latitude_degrees(),
    geodetic.longitude_degrees(),
    340.0,
  });

  require_near(core_sample.terrain_elevation_m, visual_sample.terrain_height_m, 1.0e-9,
               "CoreSim and presentation should read identical terrain height");
  require_near(core_sample.clearance_m, visual_sample.clearance_m, 1.0e-9,
               "CoreSim and presentation should read identical clearance");
  require_near(core_sample.surface_normal_ned.x, visual_sample.normal_enu.y, 1.0e-12,
               "CoreSim NED north should match presentation ENU north");
  require_near(core_sample.surface_normal_ned.y, visual_sample.normal_enu.x, 1.0e-12,
               "CoreSim NED east should match presentation ENU east");
  require_near(core_sample.surface_normal_ned.z, -visual_sample.normal_enu.z, 1.0e-12,
               "CoreSim NED down should invert presentation ENU up");
  require(core_sample.surface_material == visual_sample.surface_material,
          "CoreSim and presentation should share terrain material");
  require(core_sample.collision_available == visual_sample.collision_available,
          "CoreSim and presentation should share collision availability");
  require(core_sample.collision_watertight == visual_sample.collision_watertight,
          "CoreSim and presentation should share collision continuity");
  require_near(core_sample.collision_contact_offset_m,
               visual_sample.collision_contact_offset_m,
               0.0,
               "CoreSim and presentation should share collision contact offset");
  require(core_sample.collision_source_id == visual_sample.collision_source_id,
          "CoreSim and presentation should share collision source identity");
  require(core_sample.source_tile_id == visual_sample.source_tile_id,
          "CoreSim and presentation should share source tile identity");
  require(visual_sample.source_authority == TerrainSourceAuthority::kGenericDem,
          "Presentation should expose the GeoTerrain source authority");
}

void lod_transition_has_no_collision_height_or_normal_jumps_under_aircraft() {
  auto service = make_active_slice_service();
  constexpr double kMaxCollisionHeightJumpM = 0.20;
  constexpr double kMinNormalDot = 0.9999;

  TerrainHeightSample previous = sample_local(service, EnuVector{-80.0, 12.0, 0.0});
  for (double east_m = -70.0; east_m <= 80.0; east_m += 10.0) {
    const TerrainHeightSample current = sample_local(service, EnuVector{east_m, 12.0, 0.0});
    const double collision_previous =
        previous.height.meters + previous.collision.contact_offset_m;
    const double collision_current =
        current.height.meters + current.collision.contact_offset_m;
    const double normal_dot = previous.normal_enu.x * current.normal_enu.x +
                              previous.normal_enu.y * current.normal_enu.y +
                              previous.normal_enu.z * current.normal_enu.z;

    require(std::abs(collision_current - collision_previous) <= kMaxCollisionHeightJumpM,
            "LOD transition should not introduce sudden collision height jumps");
    require(normal_dot >= kMinNormalDot,
            "LOD transition should not introduce sudden terrain normal jumps");
    require(current.collision.available, "LOD samples should expose collision data");
    previous = current;
  }
}

} // namespace

int main() {
  coresim_and_presentation_share_terrain_height_service_api();
  lod_transition_has_no_collision_height_or_normal_jumps_under_aircraft();
  return 0;
}
