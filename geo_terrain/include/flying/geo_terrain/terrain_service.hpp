#pragma once

#include "flying/geo_terrain/geodesy.hpp"
#include "flying/geo_terrain/heights.hpp"
#include "flying/geo_terrain/math.hpp"
#include "flying/geo_terrain/units.hpp"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace flying::geo_terrain {

enum class TerrainSurfaceMaterial {
  kUnknown,
  kBareEarth,
  kGrass,
  kAsphalt,
  kConcrete,
  kGravel,
  kSoil,
  kWater,
  kSnow,
  kIce,
};

enum class TerrainSourceAuthority : std::uint8_t {
  kUnavailable = 0,
  kGenericDem = 1,
  kRunwayOverride = 2,
};

inline constexpr int kNoRunwayOverridePriority = 0;
inline constexpr int kDefaultGenericDemPriority = 100;
inline constexpr int kDefaultRunwayOverridePriority = 1'000;

struct TerrainHeightQuery {
  double latitude_rad{};
  double longitude_rad{};
};

[[nodiscard]] constexpr TerrainHeightQuery make_terrain_height_query_radians(
    double latitude_rad,
    double longitude_rad) noexcept {
  return {latitude_rad, longitude_rad};
}

[[nodiscard]] constexpr TerrainHeightQuery make_terrain_height_query_degrees(
    double latitude_deg,
    double longitude_deg) noexcept {
  return {
    units::degrees_to_radians(latitude_deg),
    units::degrees_to_radians(longitude_deg),
  };
}

struct TerrainCollisionMetadata {
  bool available{};
  bool watertight{};
  double contact_offset_m{};
  std::string source_id;
};

struct TerrainConfidenceMetadata {
  double normalized_confidence{};
  double vertical_accuracy_m{std::numeric_limits<double>::infinity()};
  double horizontal_accuracy_m{std::numeric_limits<double>::infinity()};
  bool validated{};
};

struct TerrainHeightSample {
  OrthometricHeight height{};
  Vector3d normal_enu{0.0, 0.0, 1.0};
  TerrainSurfaceMaterial surface_material{TerrainSurfaceMaterial::kUnknown};
  TerrainCollisionMetadata collision{};
  std::string source_tile_id;
  TerrainConfidenceMetadata confidence{};
  TerrainSourceAuthority source_authority{TerrainSourceAuthority::kUnavailable};
  int source_priority{};
  int runway_override_priority{kNoRunwayOverridePriority};
  bool runway_override_active{};
  std::string surface_id;
};

class ITerrainHeightService {
public:
  virtual ~ITerrainHeightService() = default;

  [[nodiscard]] virtual TerrainHeightSample sample(TerrainHeightQuery query) const = 0;
};

struct TerrainLocalBounds {
  double min_east_m{};
  double max_east_m{};
  double min_north_m{};
  double max_north_m{};

  [[nodiscard]] constexpr bool contains(double east_m, double north_m) const noexcept {
    return east_m >= min_east_m && east_m <= max_east_m &&
           north_m >= min_north_m && north_m <= max_north_m;
  }
};

struct TerrainHeightPlane {
  OrthometricHeight reference_height{};
  double reference_east_m{};
  double reference_north_m{};
  double slope_east_m_per_m{};
  double slope_north_m_per_m{};

  [[nodiscard]] constexpr OrthometricHeight height_at(
      double east_m,
      double north_m) const noexcept {
    return {
      reference_height.meters +
          (east_m - reference_east_m) * slope_east_m_per_m +
          (north_m - reference_north_m) * slope_north_m_per_m,
    };
  }
};

[[nodiscard]] constexpr TerrainHeightPlane make_flat_terrain_plane(
    OrthometricHeight height) noexcept {
  return {height, 0.0, 0.0, 0.0, 0.0};
}

[[nodiscard]] constexpr TerrainHeightPlane make_sloped_terrain_plane(
    OrthometricHeight reference_height,
    double reference_east_m,
    double reference_north_m,
    double slope_east_m_per_m,
    double slope_north_m_per_m) noexcept {
  return {
    reference_height,
    reference_east_m,
    reference_north_m,
    slope_east_m_per_m,
    slope_north_m_per_m,
  };
}

[[nodiscard]] Vector3d terrain_plane_normal_enu(TerrainHeightPlane plane) noexcept;

struct InMemoryDemSurface {
  TerrainLocalBounds bounds{};
  TerrainHeightPlane plane{};
  TerrainSurfaceMaterial surface_material{TerrainSurfaceMaterial::kBareEarth};
  TerrainCollisionMetadata collision{true, false, 0.0, {}};
  std::string source_tile_id;
  TerrainConfidenceMetadata confidence{1.0, 0.0, 0.0, false};
  int priority{kDefaultGenericDemPriority};
  std::string surface_id;
};

struct InMemoryRunwaySurfaceOverride {
  TerrainLocalBounds bounds{};
  TerrainHeightPlane plane{};
  TerrainSurfaceMaterial surface_material{TerrainSurfaceMaterial::kAsphalt};
  TerrainCollisionMetadata collision{true, true, 0.0, {}};
  std::string source_tile_id;
  TerrainConfidenceMetadata confidence{1.0, 0.0, 0.0, true};
  int runway_override_priority{kDefaultRunwayOverridePriority};
  std::string runway_id;
};

class InMemoryTerrainHeightService final : public ITerrainHeightService {
public:
  explicit InMemoryTerrainHeightService(
      GeodeticCoordinates local_origin = make_geodetic_degrees(0.0, 0.0, {0.0})) noexcept;

  void add_dem_surface(InMemoryDemSurface surface);
  void add_runway_override(InMemoryRunwaySurfaceOverride override);

  [[nodiscard]] TerrainHeightSample sample(TerrainHeightQuery query) const override;
  [[nodiscard]] EnuVector local_enu_for(TerrainHeightQuery query) const noexcept;
  [[nodiscard]] TerrainHeightQuery query_from_local_enu(EnuVector local_enu) const noexcept;
  [[nodiscard]] const LocalTangentFrame& local_frame() const noexcept;

private:
  struct Layer {
    TerrainLocalBounds bounds{};
    TerrainHeightPlane plane{};
    TerrainSurfaceMaterial surface_material{TerrainSurfaceMaterial::kUnknown};
    TerrainCollisionMetadata collision{};
    std::string source_tile_id;
    TerrainConfidenceMetadata confidence{};
    TerrainSourceAuthority source_authority{TerrainSourceAuthority::kUnavailable};
    int source_priority{};
    int runway_override_priority{kNoRunwayOverridePriority};
    std::string surface_id;
  };

  [[nodiscard]] TerrainHeightSample make_sample_from_layer(const Layer& layer,
                                                           double east_m,
                                                           double north_m) const;
  [[nodiscard]] bool layer_has_higher_priority(const Layer& candidate,
                                               const TerrainHeightSample& selected) const noexcept;

  LocalTangentFrame local_frame_{};
  std::vector<Layer> layers_;
};

} // namespace flying::geo_terrain
