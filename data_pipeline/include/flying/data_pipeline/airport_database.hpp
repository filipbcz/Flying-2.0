#pragma once

#include "flying/data_pipeline/manifest.hpp"

#include <filesystem>
#include <string_view>

namespace flying::data_pipeline {

ValidationReport validate_airport_database_text(std::string_view airport_database_json);
ValidationReport validate_airport_database_file(const std::filesystem::path& path);
ValidationReport validate_regional_airport_coverage_text(
  std::string_view airport_database_json,
  std::string_view regional_master_list_json,
  std::string_view runway_surfaces_package_json);
ValidationReport validate_regional_airport_coverage_files(
  const std::filesystem::path& airport_database_path,
  const std::filesystem::path& regional_master_list_path,
  const std::filesystem::path& runway_surfaces_package_path);

} // namespace flying::data_pipeline
