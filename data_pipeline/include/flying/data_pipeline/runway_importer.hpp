#pragma once

#include "flying/data_pipeline/manifest.hpp"

#include <filesystem>
#include <string>

namespace flying::data_pipeline {

struct RunwayImportOptions {
  std::filesystem::path airport_database_path;
  std::filesystem::path output_directory;
  std::filesystem::path report_path;
  std::string package_name = "flying-pilot-runway-surfaces";
  std::string package_version;
};

struct RunwayImportResult {
  ValidationReport report;
  std::filesystem::path package_manifest_path;

  [[nodiscard]] bool created() const noexcept {
    return report.passed && !package_manifest_path.empty();
  }
};

RunwayImportResult import_pilot_runways(const RunwayImportOptions& options);

} // namespace flying::data_pipeline
