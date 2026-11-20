#pragma once

#include "flying/core_sim/scenario.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <functional>

namespace flying::core_sim {

struct RegionalBounds {
  double min_latitude_deg{};
  double max_latitude_deg{};
  double min_longitude_deg{};
  double max_longitude_deg{};
};

struct SelectedPackageConfiguration {
  std::string package_id;
  std::string version;
  std::string manifest_sha256;
  std::filesystem::path data_root;
  std::filesystem::path package_location;
  std::string crs;
  double origin_latitude_deg{};
  double origin_longitude_deg{};
  double origin_height_m{};
  RegionalBounds bounds;
  std::vector<PilotScenarioLocation> locations;
  std::uint64_t generation{};
};

using TerrainElevationProvider = std::function<std::optional<double>(const SelectedPackageConfiguration&, double, double)>;

class FLYING_CORE_SIM_API SelectedPackageRuntime {
public:
  void select(const std::filesystem::path& data_root,
              const std::filesystem::path& relative_package,
              std::string_view expected_manifest_sha256);
  void clear() noexcept;
  [[nodiscard]] const SelectedPackageConfiguration& current() const;
  [[nodiscard]] std::uint64_t generation() const noexcept;
  [[nodiscard]] ScenarioSelection selection(std::string location_id, ScenarioStartMode mode) const;
  [[nodiscard]] ScenarioInitialState start(const ScenarioSelection& selection,
                                          bool aircraft_available) const;
  [[nodiscard]] ScenarioInitialState airborne(const ScenarioSelection& selection,
                                             PilotScenarioLocation position,
                                             double altitude_m,
                                             bool aircraft_available) const;
  void revalidate() const;
  void set_terrain_elevation_provider(TerrainElevationProvider provider);

private:
  std::optional<SelectedPackageConfiguration> selected_;
  std::uint64_t generation_{};
  TerrainElevationProvider terrain_elevation_;
};

[[nodiscard]] FLYING_CORE_SIM_API SelectedPackageRuntime& selected_package_runtime();

} // namespace flying::core_sim
