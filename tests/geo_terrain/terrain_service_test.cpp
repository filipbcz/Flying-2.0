#include "flying/geo_terrain/terrain_service.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

using flying::geo_terrain::EnuVector;
using flying::geo_terrain::InMemoryDemSurface;
using flying::geo_terrain::InMemoryRunwaySurfaceOverride;
using flying::geo_terrain::InMemoryTerrainHeightService;
using flying::geo_terrain::OrthometricHeight;
using flying::geo_terrain::TerrainConfidenceMetadata;
using flying::geo_terrain::TerrainCollisionMetadata;
using flying::geo_terrain::TerrainLocalBounds;
using flying::geo_terrain::TerrainSourceAuthority;
using flying::geo_terrain::TerrainSurfaceMaterial;
using flying::geo_terrain::kNoRunwayOverridePriority;
using flying::geo_terrain::make_flat_terrain_plane;
using flying::geo_terrain::make_geodetic_degrees;
using flying::geo_terrain::make_sloped_terrain_plane;

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

[[nodiscard]] InMemoryTerrainHeightService make_service() {
  return InMemoryTerrainHeightService{make_geodetic_degrees(49.2, 16.6, {300.0})};
}

void flat_surface_returns_complete_contract_metadata() {
  auto service = make_service();
  service.add_dem_surface({
    TerrainLocalBounds{-500.0, 500.0, -500.0, 500.0},
    make_flat_terrain_plane(OrthometricHeight{312.5}),
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, true, 0.05, "flat-collision"},
    "dem-flat-tile",
    TerrainConfidenceMetadata{0.93, 0.4, 1.2, true},
    25,
    "flat-grass",
  });

  const auto sample = service.sample(service.query_from_local_enu(EnuVector{25.0, -40.0, 0.0}));

  require_near(sample.height.meters, 312.5, 1.0e-9, "flat terrain height should match");
  require_near(sample.normal_enu.x, 0.0, 1.0e-12, "flat terrain normal east should be zero");
  require_near(sample.normal_enu.y, 0.0, 1.0e-12, "flat terrain normal north should be zero");
  require_near(sample.normal_enu.z, 1.0, 1.0e-12, "flat terrain normal should point up");
  require(sample.surface_material == TerrainSurfaceMaterial::kGrass,
          "flat terrain should return its surface material");
  require(sample.collision.available, "flat terrain should expose collision availability");
  require(sample.collision.watertight, "flat terrain should expose collision continuity");
  require_near(sample.collision.contact_offset_m, 0.05, 0.0,
               "flat terrain should expose collision contact offset");
  require(sample.collision.source_id == "flat-collision",
          "flat terrain should expose collision source identity");
  require(sample.source_tile_id == "dem-flat-tile",
          "flat terrain should expose source tile identity");
  require_near(sample.confidence.normalized_confidence, 0.93, 0.0,
               "flat terrain should expose confidence score");
  require_near(sample.confidence.vertical_accuracy_m, 0.4, 0.0,
               "flat terrain should expose vertical accuracy");
  require_near(sample.confidence.horizontal_accuracy_m, 1.2, 0.0,
               "flat terrain should expose horizontal accuracy");
  require(sample.confidence.validated, "flat terrain should expose validation status");
  require(sample.source_authority == TerrainSourceAuthority::kGenericDem,
          "flat terrain should identify generic DEM authority");
  require(sample.source_priority == 25, "flat terrain should expose source priority");
  require(sample.runway_override_priority == kNoRunwayOverridePriority,
          "generic DEM terrain should not report runway override priority");
  require(!sample.runway_override_active,
          "generic DEM terrain should not report active runway override");
  require(sample.surface_id == "flat-grass", "flat terrain should expose surface identity");
}

void sloped_surface_returns_plane_height_and_normal() {
  auto service = make_service();
  service.add_dem_surface({
    TerrainLocalBounds{-200.0, 200.0, -200.0, 200.0},
    make_sloped_terrain_plane(OrthometricHeight{100.0}, 0.0, 0.0, 0.10, -0.05),
    TerrainSurfaceMaterial::kBareEarth,
    TerrainCollisionMetadata{true, false, 0.10, "slope-collision"},
    "dem-slope-tile",
    TerrainConfidenceMetadata{0.80, 1.5, 2.0, false},
    10,
    "slope",
  });

  const auto sample = service.sample(service.query_from_local_enu(EnuVector{20.0, -10.0, 0.0}));
  const double normal_length = std::sqrt(0.10 * 0.10 + 0.05 * 0.05 + 1.0);

  require_near(sample.height.meters, 102.5, 1.0e-6,
               "sloped terrain height should evaluate the local height plane");
  require_near(sample.normal_enu.x, -0.10 / normal_length, 1.0e-12,
               "sloped terrain normal east should oppose east height gradient");
  require_near(sample.normal_enu.y, 0.05 / normal_length, 1.0e-12,
               "sloped terrain normal north should oppose north height gradient");
  require_near(sample.normal_enu.z, 1.0 / normal_length, 1.0e-12,
               "sloped terrain normal should be normalized");
  require(sample.source_tile_id == "dem-slope-tile",
          "sloped terrain should preserve source tile identity");
}

void discontinuous_surfaces_do_not_interpolate_across_tiles() {
  auto service = make_service();
  service.add_dem_surface({
    TerrainLocalBounds{-100.0, -0.1, -100.0, 100.0},
    make_flat_terrain_plane(OrthometricHeight{120.0}),
    TerrainSurfaceMaterial::kSoil,
    TerrainCollisionMetadata{true, false, 0.0, "west-collision"},
    "dem-west-tile",
    TerrainConfidenceMetadata{0.75, 2.0, 3.0, false},
    10,
    "west-step",
  });
  service.add_dem_surface({
    TerrainLocalBounds{0.1, 100.0, -100.0, 100.0},
    make_flat_terrain_plane(OrthometricHeight{165.0}),
    TerrainSurfaceMaterial::kGravel,
    TerrainCollisionMetadata{true, false, 0.0, "east-collision"},
    "dem-east-tile",
    TerrainConfidenceMetadata{0.75, 2.0, 3.0, false},
    10,
    "east-step",
  });

  const auto west = service.sample(service.query_from_local_enu(EnuVector{-1.0, 0.0, 0.0}));
  const auto east = service.sample(service.query_from_local_enu(EnuVector{1.0, 0.0, 0.0}));

  require_near(west.height.meters, 120.0, 1.0e-9,
               "west side of discontinuity should keep west tile height");
  require_near(east.height.meters, 165.0, 1.0e-9,
               "east side of discontinuity should keep east tile height");
  require(west.source_tile_id == "dem-west-tile",
          "west side of discontinuity should keep west source tile identity");
  require(east.source_tile_id == "dem-east-tile",
          "east side of discontinuity should keep east source tile identity");
  require(east.height.meters - west.height.meters == 45.0,
          "discontinuous terrain should preserve abrupt height changes");
}

void runway_override_wins_over_generic_terrain_and_runway_priority_orders_overrides() {
  auto service = make_service();
  service.add_dem_surface({
    TerrainLocalBounds{-300.0, 300.0, -300.0, 300.0},
    make_sloped_terrain_plane(OrthometricHeight{200.0}, 0.0, 0.0, 0.20, 0.0),
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, false, 0.2, "generic-collision"},
    "dem-generic-tile",
    TerrainConfidenceMetadata{0.60, 3.0, 5.0, false},
    500,
    "generic-grass",
  });
  service.add_runway_override({
    TerrainLocalBounds{10.0, 20.0, -5.0, 5.0},
    make_flat_terrain_plane(OrthometricHeight{198.0}),
    TerrainSurfaceMaterial::kAsphalt,
    TerrainCollisionMetadata{true, true, 0.0, "runway-high-collision"},
    "runway-high-tile",
    TerrainConfidenceMetadata{0.99, 0.05, 0.10, true},
    37,
    "RWY-09-high",
  });
  service.add_runway_override({
    TerrainLocalBounds{10.0, 20.0, -5.0, 5.0},
    make_flat_terrain_plane(OrthometricHeight{199.0}),
    TerrainSurfaceMaterial::kConcrete,
    TerrainCollisionMetadata{true, true, 0.0, "runway-low-collision"},
    "runway-low-tile",
    TerrainConfidenceMetadata{0.95, 0.10, 0.20, true},
    5,
    "RWY-09-low",
  });

  const auto on_runway = service.sample(service.query_from_local_enu(EnuVector{15.0, 0.0, 0.0}));
  const auto generic = service.sample(service.query_from_local_enu(EnuVector{25.0, 0.0, 0.0}));

  require_near(on_runway.height.meters, 198.0, 1.0e-6,
               "runway override should replace generic DEM height");
  require(on_runway.surface_material == TerrainSurfaceMaterial::kAsphalt,
          "highest priority runway override should replace generic DEM material");
  require(on_runway.source_authority == TerrainSourceAuthority::kRunwayOverride,
          "runway override should report runway authority");
  require(on_runway.source_tile_id == "runway-high-tile",
          "highest priority runway override should supply source tile identity");
  require(on_runway.collision.source_id == "runway-high-collision",
          "highest priority runway override should supply collision metadata");
  require(on_runway.runway_override_priority == 37,
          "runway override should expose the winning runway priority");
  require(on_runway.runway_override_active,
          "runway override should report that the runway layer is active");
  require(on_runway.surface_id == "RWY-09-high",
          "runway override should expose the winning runway identity");

  require_near(generic.height.meters, 205.0, 1.0e-6,
               "outside runway bounds generic DEM terrain should remain active");
  require(generic.surface_material == TerrainSurfaceMaterial::kGrass,
          "outside runway bounds generic DEM material should remain active");
  require(generic.source_authority == TerrainSourceAuthority::kGenericDem,
          "outside runway bounds generic DEM authority should remain active");
  require(generic.runway_override_priority == kNoRunwayOverridePriority,
          "outside runway bounds no runway priority should be reported");
}

} // namespace

int main() {
  flat_surface_returns_complete_contract_metadata();
  sloped_surface_returns_plane_height_and_normal();
  discontinuous_surfaces_do_not_interpolate_across_tiles();
  runway_override_wins_over_generic_terrain_and_runway_priority_orders_overrides();
  return 0;
}
