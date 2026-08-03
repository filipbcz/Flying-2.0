#pragma once

#include "flying/data_pipeline/manifest.hpp"

#include <filesystem>
#include <string>

namespace flying::data_pipeline {

struct Dmr5gPilotTerrainOptions {
  std::filesystem::path source_manifest_path;
  std::filesystem::path source_root;
  std::filesystem::path terrain_config_path;
  std::filesystem::path output_directory;
  std::filesystem::path report_path;
  std::string package_name = "flying-dmr5g-pilot-terrain";
  std::string package_version;
  bool verify_checksums = true;
};

struct Dmr5gPilotTerrainResult {
  ValidationReport report;
  std::filesystem::path package_manifest_path;

  [[nodiscard]] bool created() const noexcept {
    return report.passed && !package_manifest_path.empty();
  }
};

Dmr5gPilotTerrainResult process_dmr5g_pilot_terrain(
  const Dmr5gPilotTerrainOptions& options);

} // namespace flying::data_pipeline
