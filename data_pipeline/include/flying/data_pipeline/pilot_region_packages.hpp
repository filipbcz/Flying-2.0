#pragma once

#include "flying/data_pipeline/manifest.hpp"

#include <filesystem>
#include <string>

namespace flying::data_pipeline {

struct PilotRegionPackageOptions {
  std::filesystem::path source_manifest_path;
  std::filesystem::path region_manifest_path;
  std::filesystem::path source_root;
  std::filesystem::path package_config_path;
  std::filesystem::path output_directory;
  std::filesystem::path report_path;
  std::string package_name = "flying-pilot-region-offline-gis";
  std::string package_version;
  bool verify_checksums = true;
  bool require_czech_republic_scope = false;
};

using CzechRepublicPackageOptions = PilotRegionPackageOptions;

struct PilotRegionPackageResult {
  ValidationReport report;
  std::filesystem::path package_manifest_path;

  [[nodiscard]] bool created() const noexcept {
    return report.passed && !package_manifest_path.empty();
  }
};

PilotRegionPackageResult process_pilot_region_packages(
  const PilotRegionPackageOptions& options);

PilotRegionPackageResult process_czech_republic_packages(
  const CzechRepublicPackageOptions& options);

} // namespace flying::data_pipeline
