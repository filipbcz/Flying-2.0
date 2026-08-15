#pragma once

#include "TerrainHeightService.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace flying::geo_terrain {

enum class RunwaySurfaceKind {
  kRunway,
  kTaxiway,
  kSlzStrip,
};

enum class RunwaySurfaceLayer {
  kVisual,
  kCollision,
};

struct RunwaySurfacePoint {
  double station_m{};
  double lateral_m{};
  double east_m{};
  double north_m{};
  double height_m{};
};

struct RunwaySurfaceMesh {
  std::vector<RunwaySurfacePoint> vertices;
  std::vector<std::uint32_t> indices;
  double max_lod_error_m{};
};

struct RunwaySurfaceBlendBand {
  double lateral_sign{1.0};
  double width_m{};
  double inner_height_m{};
  double outer_height_m{};
  TerrainSurfaceMaterial terrain_material{TerrainSurfaceMaterial::kBareEarth};

  [[nodiscard]] constexpr bool passes_transition_limit(double max_step_m) const noexcept {
    return width_m > 0.0 && std::abs(outer_height_m - inner_height_m) <= max_step_m;
  }
};

struct RunwaySurfaceMetadata {
  std::string aerodrome_id;
  std::string runway_id;
  std::string source_id;
  RunwaySurfaceKind kind{RunwaySurfaceKind::kRunway};
  TerrainSurfaceMaterial material{TerrainSurfaceMaterial::kAsphalt};
  double roughness_m{};
  bool has_centerline_marking{};
  bool has_edge_marking{};
  bool has_threshold_marking{};
  double longitudinal_slope_percent{};
  double transverse_slope_percent{};
};

struct RunwaySurfaceDefinition {
  RunwaySurfaceMetadata metadata;
  double center_east_m{};
  double center_north_m{};
  double true_bearing_deg{};
  double length_m{};
  double width_m{};
  double reference_height_m{};
  double longitudinal_slope_percent{};
  double transverse_slope_percent{};
  double collision_height_bias_m{};
  double transition_width_m{8.0};
  TerrainSurfaceMaterial surrounding_material{TerrainSurfaceMaterial::kGrass};
};

struct RunwaySurfaceSample {
  bool hit{};
  std::string runway_id;
  TerrainSurfaceMaterial material{TerrainSurfaceMaterial::kUnknown};
  double visual_height_m{};
  double collision_height_m{};
  double longitudinal_slope_percent{};
  double transverse_slope_percent{};
  double roughness_m{};
};

struct RunwayPhysicalSurface {
  RunwaySurfaceMetadata metadata;
  TerrainLocalBounds bounds;
  RunwaySurfaceMesh visual_mesh;
  RunwaySurfaceMesh collision_mesh;
  std::vector<RunwaySurfaceBlendBand> transition_bands;
  double center_east_m{};
  double center_north_m{};
  double heading_rad{};
  double length_m{};
  double width_m{};
  double reference_height_m{};
  double collision_height_bias_m{};

  [[nodiscard]] double transition_width_m() const noexcept {
    double width = 0.0;
    for (const auto& band : transition_bands) {
      width = std::max(width, band.width_m);
    }
    return width;
  }

  [[nodiscard]] constexpr double local_station(double east_m, double north_m) const noexcept {
    const double de = east_m - center_east_m;
    const double dn = north_m - center_north_m;
    return std::sin(heading_rad) * de + std::cos(heading_rad) * dn;
  }

  [[nodiscard]] constexpr double local_lateral(double east_m, double north_m) const noexcept {
    const double de = east_m - center_east_m;
    const double dn = north_m - center_north_m;
    return std::cos(heading_rad) * de - std::sin(heading_rad) * dn;
  }

  [[nodiscard]] constexpr bool contains(double east_m, double north_m) const noexcept {
    return bounds.contains(east_m, north_m) &&
           std::abs(local_station(east_m, north_m)) <= length_m * 0.5 &&
           std::abs(local_lateral(east_m, north_m)) <= width_m * 0.5;
  }

  [[nodiscard]] bool contains_physical_surface(double east_m, double north_m) const noexcept {
    return std::abs(local_station(east_m, north_m)) <= length_m * 0.5 &&
           std::abs(local_lateral(east_m, north_m)) <= width_m * 0.5 + transition_width_m();
  }

  [[nodiscard]] constexpr double visual_height_at(double station_m,
                                                  double lateral_m) const noexcept {
    return reference_height_m +
           station_m * (metadata.longitudinal_slope_percent / 100.0) +
           lateral_m * (metadata.transverse_slope_percent / 100.0);
  }

  [[nodiscard]] constexpr double collision_height_at(double station_m,
                                                     double lateral_m) const noexcept {
    return visual_height_at(station_m, lateral_m) + collision_height_bias_m;
  }

  [[nodiscard]] std::optional<RunwaySurfaceSample> sample_transition(double station_m,
                                                                     double lateral_m) const {
    const double half_width = width_m * 0.5;
    const double abs_lateral = std::abs(lateral_m);
    if (abs_lateral <= half_width) {
      return std::nullopt;
    }
    const double lateral_sign = lateral_m < 0.0 ? -1.0 : 1.0;
    const auto band_iter = std::find_if(
        transition_bands.begin(),
        transition_bands.end(),
        [lateral_sign](const RunwaySurfaceBlendBand& band) {
          return band.lateral_sign == 0.0 || band.lateral_sign == lateral_sign;
        });
    if (band_iter == transition_bands.end()) {
      return std::nullopt;
    }
    const double distance_into_transition = abs_lateral - half_width;
    if (distance_into_transition > band_iter->width_m) {
      return std::nullopt;
    }
    const double t = band_iter->width_m > 0.0 ? distance_into_transition / band_iter->width_m : 1.0;
    const double edge_lateral = lateral_sign * half_width;
    const double runway_edge_height = visual_height_at(station_m, edge_lateral);
    const double terrain_height = band_iter->outer_height_m +
                                  station_m * (metadata.longitudinal_slope_percent / 100.0);
    const double blended_height = runway_edge_height * (1.0 - t) + terrain_height * t;
    return RunwaySurfaceSample{
      true,
      metadata.runway_id,
      band_iter->terrain_material,
      blended_height,
      blended_height + collision_height_bias_m,
      metadata.longitudinal_slope_percent,
      metadata.transverse_slope_percent * (1.0 - t),
      metadata.roughness_m,
    };
  }
};

[[nodiscard]] inline double runway_surface_roughness(TerrainSurfaceMaterial material) noexcept {
  switch (material) {
  case TerrainSurfaceMaterial::kAsphalt:
  case TerrainSurfaceMaterial::kConcrete:
    return 0.012;
  case TerrainSurfaceMaterial::kGrass:
    return 0.045;
  case TerrainSurfaceMaterial::kGravel:
    return 0.060;
  default:
    return 0.080;
  }
}

[[nodiscard]] inline TerrainLocalBounds bounds_for_runway_rectangle(double center_east_m,
                                                                    double center_north_m,
                                                                    double heading_rad,
                                                                    double length_m,
                                                                    double width_m) noexcept {
  TerrainLocalBounds bounds{
    std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(),
    std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(),
  };
  for (double station : {-length_m * 0.5, length_m * 0.5}) {
    for (double lateral : {-width_m * 0.5, width_m * 0.5}) {
      const double east = center_east_m + std::sin(heading_rad) * station +
                          std::cos(heading_rad) * lateral;
      const double north = center_north_m + std::cos(heading_rad) * station -
                           std::sin(heading_rad) * lateral;
      bounds.min_east_m = std::min(bounds.min_east_m, east);
      bounds.max_east_m = std::max(bounds.max_east_m, east);
      bounds.min_north_m = std::min(bounds.min_north_m, north);
      bounds.max_north_m = std::max(bounds.max_north_m, north);
    }
  }
  return bounds;
}

[[nodiscard]] inline RunwaySurfaceMesh make_runway_mesh(const RunwaySurfaceDefinition& definition,
                                                        bool collision) {
  constexpr std::size_t kStationSegments = 8;
  constexpr std::size_t kLateralSegments = 2;
  const double heading_rad = units::degrees_to_radians(definition.true_bearing_deg);
  const double height_bias = collision ? definition.collision_height_bias_m : 0.0;

  RunwaySurfaceMesh mesh;
  mesh.max_lod_error_m = collision ? 0.0 : 0.025;
  for (std::size_t s = 0; s <= kStationSegments; ++s) {
    const double station = -definition.length_m * 0.5 +
                           definition.length_m * static_cast<double>(s) /
                               static_cast<double>(kStationSegments);
    for (std::size_t l = 0; l <= kLateralSegments; ++l) {
      const double lateral = -definition.width_m * 0.5 +
                             definition.width_m * static_cast<double>(l) /
                                 static_cast<double>(kLateralSegments);
      const double east = definition.center_east_m + std::sin(heading_rad) * station +
                          std::cos(heading_rad) * lateral;
      const double north = definition.center_north_m + std::cos(heading_rad) * station -
                           std::sin(heading_rad) * lateral;
      const double height = definition.reference_height_m +
                            station * (definition.longitudinal_slope_percent / 100.0) +
                            lateral * (definition.transverse_slope_percent / 100.0) +
                            height_bias;
      mesh.vertices.push_back({station, lateral, east, north, height});
    }
  }

  for (std::size_t s = 0; s < kStationSegments; ++s) {
    for (std::size_t l = 0; l < kLateralSegments; ++l) {
      const std::uint32_t a = static_cast<std::uint32_t>(s * (kLateralSegments + 1) + l);
      const std::uint32_t b = static_cast<std::uint32_t>((s + 1) * (kLateralSegments + 1) + l);
      const std::uint32_t c = b + 1;
      const std::uint32_t d = a + 1;
      mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
    }
  }
  return mesh;
}

[[nodiscard]] inline RunwayPhysicalSurface make_runway_physical_surface(
    RunwaySurfaceDefinition definition) {
  definition.metadata.longitudinal_slope_percent = definition.longitudinal_slope_percent;
  definition.metadata.transverse_slope_percent = definition.transverse_slope_percent;
  if (definition.metadata.roughness_m <= 0.0) {
    definition.metadata.roughness_m = runway_surface_roughness(definition.metadata.material);
  }

  const double heading_rad = units::degrees_to_radians(definition.true_bearing_deg);
  const double half_width = definition.width_m * 0.5;
  const double inner_height = definition.reference_height_m +
                              half_width * (definition.transverse_slope_percent / 100.0);
  const double outer_height = definition.reference_height_m +
                              (half_width + definition.transition_width_m) *
                                  (definition.transverse_slope_percent / 100.0);
  const double left_inner_height = definition.reference_height_m -
                                   half_width * (definition.transverse_slope_percent / 100.0);
  const double left_outer_height = definition.reference_height_m -
                                   (half_width + definition.transition_width_m) *
                                       (definition.transverse_slope_percent / 100.0);
  return {
    std::move(definition.metadata),
    bounds_for_runway_rectangle(definition.center_east_m,
                                definition.center_north_m,
                                heading_rad,
                                definition.length_m,
                                definition.width_m + 2.0 * definition.transition_width_m),
    make_runway_mesh(definition, false),
    make_runway_mesh(definition, true),
    {
      RunwaySurfaceBlendBand{
        -1.0,
        definition.transition_width_m,
        left_inner_height,
        left_outer_height,
        definition.surrounding_material,
      },
      RunwaySurfaceBlendBand{
        1.0,
        definition.transition_width_m,
        inner_height,
        outer_height,
        definition.surrounding_material,
      },
    },
    definition.center_east_m,
    definition.center_north_m,
    heading_rad,
    definition.length_m,
    definition.width_m,
    definition.reference_height_m,
    definition.collision_height_bias_m,
  };
}

class RunwaySurfaceProvider {
public:
  void add_surface(RunwayPhysicalSurface surface) {
    surfaces_.push_back(std::move(surface));
  }

  [[nodiscard]] const std::vector<RunwayPhysicalSurface>& surfaces() const noexcept {
    return surfaces_;
  }

  [[nodiscard]] std::optional<RunwaySurfaceSample> sample(double east_m,
                                                          double north_m) const {
    for (const auto& surface : surfaces_) {
      if (!surface.contains_physical_surface(east_m, north_m)) {
        continue;
      }
      const double station = surface.local_station(east_m, north_m);
      const double lateral = surface.local_lateral(east_m, north_m);
      if (!surface.contains(east_m, north_m)) {
        if (const auto transition_sample = surface.sample_transition(station, lateral)) {
          return transition_sample;
        }
        continue;
      }
      return RunwaySurfaceSample{
        true,
        surface.metadata.runway_id,
        surface.metadata.material,
        surface.visual_height_at(station, lateral),
        surface.collision_height_at(station, lateral),
        surface.metadata.longitudinal_slope_percent,
        surface.metadata.transverse_slope_percent,
        surface.metadata.roughness_m,
      };
    }
    return std::nullopt;
  }

private:
  std::vector<RunwayPhysicalSurface> surfaces_;
};

} // namespace flying::geo_terrain
