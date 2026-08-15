#pragma once

#include "TerrainHeightService.h"

#include <string>

namespace flying::presentation {

struct CesiumTerrainQuery {
  double latitude_deg{};
  double longitude_deg{};
  double ellipsoidal_height_m{};
};

struct CesiumTerrainSample {
  double latitude_deg{};
  double longitude_deg{};
  double terrain_height_m{};
  double clearance_m{};
  geo_terrain::Vector3d normal_enu{0.0, 0.0, 1.0};
  geo_terrain::TerrainSurfaceMaterial surface_material{
      geo_terrain::TerrainSurfaceMaterial::kUnknown};
  bool collision_available{};
  bool collision_watertight{};
  double collision_contact_offset_m{};
  std::string collision_source_id;
  std::string source_tile_id;
  geo_terrain::TerrainSourceAuthority source_authority{
      geo_terrain::TerrainSourceAuthority::kUnavailable};
};

class TerrainCesiumAdapter {
public:
  explicit TerrainCesiumAdapter(const geo_terrain::ITerrainHeightService& terrain_service) noexcept;

  [[nodiscard]] CesiumTerrainSample sample_for_visual_tile(CesiumTerrainQuery query) const;

private:
  const geo_terrain::ITerrainHeightService& terrain_service_;
};

} // namespace flying::presentation

