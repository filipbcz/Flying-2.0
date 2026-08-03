#pragma once

#include "flying/data_pipeline/manifest.hpp"

#include <filesystem>
#include <string_view>

namespace flying::data_pipeline {

ValidationReport validate_airport_database_text(std::string_view airport_database_json);
ValidationReport validate_airport_database_file(const std::filesystem::path& path);

} // namespace flying::data_pipeline
