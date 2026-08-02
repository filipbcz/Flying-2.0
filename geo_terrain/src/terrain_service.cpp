#include "flying/geo_terrain/terrain_service.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace flying::geo_terrain {
namespace {

[[nodiscard]] bool is_finite(double value) noexcept {
  return std::isfinite(value);
}

void require_finite(double value, const char* field_name) {
  if (!is_finite(value)) {
    throw std::invalid_argument(std::string(field_name) + " must be finite");
  }
}

void validate_query(TerrainHeightQuery query) {
  require_finite(query.latitude_rad, "latitude_rad");
  require_finite(query.longitude_rad, "longitude_rad");
  if (query.latitude_rad < -kPi / 2.0 || query.latitude_rad > kPi / 2.0) {
    throw std::invalid_argument("latitude_rad must be in the [-pi/2, pi/2] range");
  }
  if (query.longitude_rad < -kPi || query.longitude_rad > kPi) {
    throw std::invalid_argument("longitude_rad must be in the [-pi, pi] range");
  }
}

void validate_bounds(TerrainLocalBounds bounds) {
  require_finite(bounds.min_east_m, "min_east_m");
  require_finite(bounds.max_east_m, "max_east_m");
  require_finite(bounds.min_north_m, "min_north_m");
  require_finite(bounds.max_north_m, "max_north_m");
  if (bounds.min_east_m > bounds.max_east_m) {
    throw std::invalid_argument("terrain local bounds east interval must be ordered");
  }
  if (bounds.min_north_m > bounds.max_north_m) {
    throw std::invalid_argument("terrain local bounds north interval must be ordered");
  }
}

void validate_plane(TerrainHeightPlane plane) {
  require_finite(plane.reference_height.meters, "reference_height");
  require_finite(plane.reference_east_m, "reference_east_m");
  require_finite(plane.reference_north_m, "reference_north_m");
  require_finite(plane.slope_east_m_per_m, "slope_east_m_per_m");
  require_finite(plane.slope_north_m_per_m, "slope_north_m_per_m");
}

void validate_collision(TerrainCollisionMetadata collision) {
  require_finite(collision.contact_offset_m, "contact_offset_m");
  if (collision.contact_offset_m < 0.0) {
    throw std::invalid_argument("contact_offset_m must be non-negative");
  }
}

void validate_confidence(TerrainConfidenceMetadata confidence) {
  require_finite(confidence.normalized_confidence, "normalized_confidence");
  require_finite(confidence.vertical_accuracy_m, "vertical_accuracy_m");
  require_finite(confidence.horizontal_accuracy_m, "horizontal_accuracy_m");
  if (confidence.normalized_confidence < 0.0 || confidence.normalized_confidence > 1.0) {
    throw std::invalid_argument("normalized_confidence must be in the [0, 1] range");
  }
  if (confidence.vertical_accuracy_m < 0.0 || confidence.horizontal_accuracy_m < 0.0) {
    throw std::invalid_argument("terrain confidence accuracy values must be non-negative");
  }
}

void validate_priority(int priority, const char* field_name) {
  if (priority < 0) {
    throw std::invalid_argument(std::string(field_name) + " must be non-negative");
  }
}

void validate_surface_definition(const TerrainLocalBounds bounds,
                                 const TerrainHeightPlane plane,
                                 const TerrainCollisionMetadata collision,
                                 const TerrainConfidenceMetadata confidence,
                                 int priority,
                                 const char* priority_field_name) {
  validate_bounds(bounds);
  validate_plane(plane);
  validate_collision(collision);
  validate_confidence(confidence);
  validate_priority(priority, priority_field_name);
}

[[nodiscard]] Vector3d normalize_or_up(Vector3d value) noexcept {
  const double length = norm(value);
  if (length <= 0.0 || !std::isfinite(length)) {
    return {0.0, 0.0, 1.0};
  }
  return value / length;
}

[[nodiscard]] int authority_rank(TerrainSourceAuthority authority) noexcept {
  switch (authority) {
    case TerrainSourceAuthority::kRunwayOverride:
      return 2;
    case TerrainSourceAuthority::kGenericDem:
      return 1;
    case TerrainSourceAuthority::kUnavailable:
      return 0;
  }

  return 0;
}

[[nodiscard]] TerrainHeightSample unavailable_sample() {
  TerrainHeightSample sample{};
  sample.source_tile_id = "unavailable";
  return sample;
}

} // namespace

Vector3d terrain_plane_normal_enu(TerrainHeightPlane plane) noexcept {
  return normalize_or_up({-plane.slope_east_m_per_m, -plane.slope_north_m_per_m, 1.0});
}

InMemoryTerrainHeightService::InMemoryTerrainHeightService(GeodeticCoordinates local_origin) noexcept
    : local_frame_(make_local_tangent_frame(local_origin)) {}

void InMemoryTerrainHeightService::add_dem_surface(InMemoryDemSurface surface) {
  validate_surface_definition(surface.bounds,
                              surface.plane,
                              surface.collision,
                              surface.confidence,
                              surface.priority,
                              "priority");

  layers_.push_back({
    surface.bounds,
    surface.plane,
    surface.surface_material,
    std::move(surface.collision),
    std::move(surface.source_tile_id),
    surface.confidence,
    TerrainSourceAuthority::kGenericDem,
    surface.priority,
    kNoRunwayOverridePriority,
    std::move(surface.surface_id),
  });
}

void InMemoryTerrainHeightService::add_runway_override(InMemoryRunwaySurfaceOverride override) {
  validate_surface_definition(override.bounds,
                              override.plane,
                              override.collision,
                              override.confidence,
                              override.runway_override_priority,
                              "runway_override_priority");

  layers_.push_back({
    override.bounds,
    override.plane,
    override.surface_material,
    std::move(override.collision),
    std::move(override.source_tile_id),
    override.confidence,
    TerrainSourceAuthority::kRunwayOverride,
    override.runway_override_priority,
    override.runway_override_priority,
    std::move(override.runway_id),
  });
}

TerrainHeightSample InMemoryTerrainHeightService::sample(TerrainHeightQuery query) const {
  validate_query(query);

  const EnuVector local = local_enu_for(query);
  TerrainHeightSample selected = unavailable_sample();
  for (const Layer& layer : layers_) {
    if (!layer.bounds.contains(local.east_m, local.north_m)) {
      continue;
    }
    if (layer_has_higher_priority(layer, selected)) {
      selected = make_sample_from_layer(layer, local.east_m, local.north_m);
    }
  }

  return selected;
}

EnuVector InMemoryTerrainHeightService::local_enu_for(TerrainHeightQuery query) const noexcept {
  const GeodeticCoordinates geodetic{
    query.latitude_rad,
    query.longitude_rad,
    local_frame_.origin_geodetic.ellipsoidal_height,
  };
  return enu_from_ecef_position(local_frame_, geodetic_to_ecef(geodetic));
}

TerrainHeightQuery InMemoryTerrainHeightService::query_from_local_enu(EnuVector local_enu) const noexcept {
  const GeodeticCoordinates geodetic = ecef_to_geodetic(ecef_from_enu(local_frame_, local_enu));
  return {geodetic.latitude_rad, geodetic.longitude_rad};
}

const LocalTangentFrame& InMemoryTerrainHeightService::local_frame() const noexcept {
  return local_frame_;
}

TerrainHeightSample InMemoryTerrainHeightService::make_sample_from_layer(const Layer& layer,
                                                                         double east_m,
                                                                         double north_m) const {
  return {
    layer.plane.height_at(east_m, north_m),
    terrain_plane_normal_enu(layer.plane),
    layer.surface_material,
    layer.collision,
    layer.source_tile_id,
    layer.confidence,
    layer.source_authority,
    layer.source_priority,
    layer.runway_override_priority,
    layer.source_authority == TerrainSourceAuthority::kRunwayOverride,
    layer.surface_id,
  };
}

bool InMemoryTerrainHeightService::layer_has_higher_priority(
    const Layer& candidate,
    const TerrainHeightSample& selected) const noexcept {
  const int candidate_authority_rank = authority_rank(candidate.source_authority);
  const int selected_authority_rank = authority_rank(selected.source_authority);
  if (candidate_authority_rank != selected_authority_rank) {
    return candidate_authority_rank > selected_authority_rank;
  }
  return candidate.source_priority > selected.source_priority;
}

} // namespace flying::geo_terrain
