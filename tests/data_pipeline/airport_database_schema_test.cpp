#include "flying/data_pipeline/airport_database.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

std::filesystem::path repo_root() {
  return std::filesystem::path{FLYING_REPO_SOURCE_DIR};
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

bool has_issue_code(const flying::data_pipeline::ValidationReport& report,
                    std::string_view code) {
  for (const flying::data_pipeline::ValidationIssue& issue : report.issues) {
    if (issue.code == code) {
      return true;
    }
  }
  return false;
}

std::string replace_all(std::string text,
                        std::string_view needle,
                        std::string_view replacement) {
  std::size_t offset = 0U;
  while (true) {
    const std::size_t position = text.find(needle, offset);
    if (position == std::string::npos) {
      return text;
    }
    text.replace(position, needle.size(), replacement);
    offset = position + replacement.size();
  }
}

std::string remove_first_paved_threshold_coordinates(std::string text) {
  const std::size_t runway = text.find("\"id\": \"FPPV-RWY-09-27\"");
  require(runway != std::string::npos, "paved runway fixture not found");
  const std::size_t physical = text.find("\"physicalThreshold\": {", runway);
  require(physical != std::string::npos, "physical threshold fixture not found");
  const std::size_t position = text.find("                  \"positionWgs84\": {", physical);
  require(position != std::string::npos, "physical threshold coordinates not found");
  const std::size_t next_field = text.find("                  \"sourceAccuracyM\"", position);
  require(next_field != std::string::npos, "physical threshold coordinate block end not found");
  text.erase(position, next_field - position);
  return text;
}

std::string remove_first_paved_runway_provenance(std::string text) {
  const std::size_t runway = text.find("\"id\": \"FPPV-RWY-09-27\"");
  require(runway != std::string::npos, "paved runway fixture not found");
  const std::size_t airac = text.find("          \"airacEffectiveDate\": \"2026-08-03\"", runway);
  require(airac != std::string::npos, "runway AIRAC field not found");
  const std::size_t provenance = text.find("          \"provenance\": [", airac);
  require(provenance != std::string::npos, "runway provenance block not found");
  const std::size_t validation = text.find("          \"validation\": {", provenance);
  require(validation != std::string::npos, "runway validation block not found");
  text.erase(provenance, validation - provenance);
  return text;
}

void schema_represents_airport_master_list_and_runways() {
  const std::string schema =
    read_file(repo_root() / "data_pipeline" / "schemas" / "airport-database.schema.json");

  for (const std::string_view required :
       {"aerodrome",
        "runway",
        "runwayEnd",
        "physicalThreshold",
        "declaredDistances",
        "surface",
        "lighting",
        "markings",
        "slope",
        "material",
        "provenance",
        "airacVersion",
        "confidence",
        "manualVerification"}) {
    require(schema.find(required) != std::string::npos,
            "airport database schema is missing an accepted runway/master-list field");
  }

  for (const std::string_view classification :
       {"active_airport",
        "slz_field",
        "closed_field",
        "validated",
        "derived",
        "blocked_missing_permission",
        "blocked_missing_source_data",
        "production_validated"}) {
    require(schema.find(classification) != std::string::npos,
            "airport database schema is missing a required classification state");
  }
}

void pilot_seed_validates_and_includes_paved_and_grass_runways() {
  const std::filesystem::path seed_path =
    repo_root() / "data_pipeline" / "seeds" / "pilot-airport-master-list.json";
  const flying::data_pipeline::ValidationReport report =
    flying::data_pipeline::validate_airport_database_file(seed_path);
  require(report.passed, "pilot airport seed database must validate");

  const std::string seed = read_file(seed_path);
  require(seed.find("\"surfaceType\": \"paved\"") != std::string::npos,
          "pilot seed must include one paved runway");
  require(seed.find("\"surfaceType\": \"grass\"") != std::string::npos,
          "pilot seed must include one grass runway");
  require(seed.find("\"sourceId\": \"project-pilot-fixture-2026-08\"") !=
            std::string::npos,
          "pilot seed must include source provenance");
  require(seed.find("\"status\": \"derived\"") != std::string::npos,
          "pilot seed must include explicit validation status");
}

void production_validated_runways_require_thresholds_and_provenance() {
  const std::string seed = read_file(
    repo_root() / "data_pipeline" / "seeds" / "pilot-airport-master-list.json");
  const std::string production_seed =
    replace_all(seed, "\"status\": \"derived\"", "\"status\": \"production_validated\"");

  const flying::data_pipeline::ValidationReport no_threshold =
    flying::data_pipeline::validate_airport_database_text(
      remove_first_paved_threshold_coordinates(production_seed));
  require(!no_threshold.passed,
          "production-validated runway without threshold coordinates must be rejected");
  require(has_issue_code(no_threshold,
                         "airport.runway_end.production_physical_threshold_coordinates.missing"),
          "missing physical threshold coordinates must report the production threshold issue");

  const flying::data_pipeline::ValidationReport no_provenance =
    flying::data_pipeline::validate_airport_database_text(
      remove_first_paved_runway_provenance(production_seed));
  require(!no_provenance.passed,
          "production-validated runway without source provenance must be rejected");
  require(has_issue_code(no_provenance, "airport.runway.production_provenance.missing"),
          "missing runway source provenance must report the production provenance issue");
}

} // namespace

int main() {
  try {
    schema_represents_airport_master_list_and_runways();
    pilot_seed_validates_and_includes_paved_and_grass_runways();
    production_validated_runways_require_thresholds_and_provenance();
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
  return 0;
}
