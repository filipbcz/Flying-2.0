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

std::string remove_json_insignificant_whitespace(std::string_view text) {
  std::string compact;
  bool in_string = false;
  bool escaping = false;
  for (const char ch : text) {
    if (escaping) {
      compact.push_back(ch);
      escaping = false;
      continue;
    }
    if (in_string && ch == '\\') {
      compact.push_back(ch);
      escaping = true;
      continue;
    }
    if (ch == '"') {
      compact.push_back(ch);
      in_string = !in_string;
      continue;
    }
    if (in_string || (ch != ' ' && ch != '\n' && ch != '\r' && ch != '\t')) {
      compact.push_back(ch);
    }
  }
  return compact;
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

std::string production_approved_airport_database() {
  std::string seed = read_file(
    repo_root() / "data_pipeline" / "seeds" / "pilot-airport-master-list.json");
  seed = replace_all(seed, "\"status\": \"derived\"", "\"status\": \"production_validated\"");
  seed = replace_all(seed, "\"status\": \"manually_verified\"",
                     "\"status\": \"production_validated\"");
  seed = replace_all(seed, "\"status\": \"unverified\"", "\"status\": \"reviewer_approved\"");
  return seed;
}

std::string regional_master_list(std::string_view extra_record = {}) {
  std::string text = R"({
  "schemaVersion": "flying.regional-airport-master-list.v1",
  "regionId": "test-region",
  "airac": {
    "cycleId": "2026-08",
    "effectiveDate": "2026-08-03",
    "source": "test fixture"
  },
  "masterList": [
    {
      "aerodromeId": "FPPV",
      "name": "Flying Pilot Paved Airport",
      "classification": "active_airport",
      "operationalStatus": "active",
      "requiredRunways": [
        {"id": "FPPV-RWY-09-27", "designator": "09/27"}
      ]
    },
    {
      "aerodromeId": "FPGS",
      "name": "Flying Pilot Grass SLZ Field",
      "classification": "slz_field",
      "operationalStatus": "active",
      "requiredRunways": [
        {"id": "FPGS-RWY-16-34", "designator": "16/34"}
      ]
    })";
  if (!extra_record.empty()) {
    text += ",\n";
    text += extra_record;
  }
  text += R"(
  ]
})";
  return text;
}

std::string runway_surfaces_package(std::string_view extra_surface = {}) {
  std::string text = R"({
  "schemaVersion": "flying.pilot-runway-surfaces-package.v1",
  "surfaces": [
    {"aerodromeId": "FPPV", "runwayId": "FPPV-RWY-09-27"},
    {"aerodromeId": "FPGS", "runwayId": "FPGS-RWY-16-34"})";
  if (!extra_surface.empty()) {
    text += ",\n";
    text += extra_surface;
  }
  text += R"(
  ]
})";
  return text;
}

std::string runway_surfaces_without_slz() {
  return R"({
  "schemaVersion": "flying.pilot-runway-surfaces-package.v1",
  "surfaces": [
    {"aerodromeId": "FPPV", "runwayId": "FPPV-RWY-09-27"}
  ]
})";
}

std::string regional_master_list_with_inactive_and_closed_records_without_runways() {
  return R"({
  "schemaVersion": "flying.regional-airport-master-list.v1",
  "regionId": "test-region",
  "masterList": [
    {
      "aerodromeId": "FPIN",
      "name": "Inactive regional field",
      "classification": "active_airport",
      "operationalStatus": "inactive"
    },
    {
      "aerodromeId": "FPCL",
      "name": "Closed regional field",
      "classification": "closed_field",
      "operationalStatus": "closed"
    }
  ]
})";
}

std::string regional_master_list_with_active_record_without_runways() {
  return R"({
  "schemaVersion": "flying.regional-airport-master-list.v1",
  "regionId": "test-region",
  "masterList": [
    {
      "aerodromeId": "FPPV",
      "name": "Flying Pilot Paved Airport",
      "classification": "active_airport",
      "operationalStatus": "active"
    }
  ]
})";
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
        "manualVerification",
        "startLocationEligibility",
        "geometryReview"}) {
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

void regional_master_list_schema_matches_runtime_required_runways_rule() {
  const std::string schema =
    read_file(repo_root() / "data_pipeline" / "schemas" /
              "regional-airport-master-list.schema.json");
  const std::string compact_schema = remove_json_insignificant_whitespace(schema);
  const std::size_t conditional = compact_schema.find("\"then\":{");
  require(conditional != std::string::npos,
          "regional master-list schema must define an active-record conditional");
  const std::size_t conditional_end =
    compact_schema.find("\"runwayRequirement\"", conditional);
  require(conditional_end != std::string::npos,
          "regional master-list schema conditional end not found");
  const std::string conditional_schema =
    compact_schema.substr(conditional, conditional_end - conditional);
  require(conditional_schema.find("\"required\":[\"requiredRunways\"]") != std::string::npos,
          "regional master-list schema must require requiredRunways for active airports and SLZ areas");
  require(conditional_schema.find("\"requiredRunways\":{\"minItems\":1") != std::string::npos,
          "regional master-list schema must reject empty active requiredRunways arrays");

  const flying::data_pipeline::ValidationReport inactive_report =
    flying::data_pipeline::validate_regional_airport_coverage_text(
      production_approved_airport_database(),
      regional_master_list_with_inactive_and_closed_records_without_runways(),
      runway_surfaces_package());
  require(inactive_report.passed,
          "inactive and closed regional records without requiredRunways must match schema and runtime validation");

  const flying::data_pipeline::ValidationReport active_report =
    flying::data_pipeline::validate_regional_airport_coverage_text(
      production_approved_airport_database(),
      regional_master_list_with_active_record_without_runways(),
      runway_surfaces_package());
  require(!active_report.passed,
          "active regional airport without requiredRunways must fail runtime validation");
  require(has_issue_code(active_report, "airport.coverage.master_list.active_runways.missing"),
          "active regional airport without requiredRunways must report missing active runway requirements");
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

void closed_aerodromes_cannot_be_default_start_locations() {
  std::string seed = read_file(
    repo_root() / "data_pipeline" / "seeds" / "pilot-airport-master-list.json");
  const std::size_t closed = seed.find("\"aerodromeId\": \"FPCL\"");
  require(closed != std::string::npos, "closed master-list fixture not found");
  const std::size_t eligibility = seed.find("\"defaultStartLocation\": false", closed);
  require(eligibility != std::string::npos,
          "closed master-list start eligibility fixture not found");
  seed.replace(eligibility,
               std::string{"\"defaultStartLocation\": false"}.size(),
               "\"defaultStartLocation\": true");

  const flying::data_pipeline::ValidationReport report =
    flying::data_pipeline::validate_airport_database_text(seed);
  require(!report.passed,
          "closed aerodrome default start location must be rejected");
  require(has_issue_code(
            report,
            "airport.master_list.startLocationEligibility.defaultStartLocation.ineligible"),
          "closed default start location must report the eligibility issue");
}

void regional_coverage_accepts_configured_master_list_and_surface_links() {
  const flying::data_pipeline::ValidationReport report =
    flying::data_pipeline::validate_regional_airport_coverage_text(
      production_approved_airport_database(),
      regional_master_list(),
      runway_surfaces_package());
  require(report.passed, "regional airport coverage must accept the configured master list");
}

void regional_coverage_fails_on_missing_runways_and_slz_areas() {
  const std::string missing_runway = R"({
      "aerodromeId": "FPPV",
      "name": "Missing regional runway",
      "classification": "active_airport",
      "operationalStatus": "active",
      "requiredRunways": [
        {"id": "FPPV-RWY-10-28", "designator": "10/28"}
      ]
    })";
  flying::data_pipeline::ValidationReport runway_report =
    flying::data_pipeline::validate_regional_airport_coverage_text(
      production_approved_airport_database(),
      regional_master_list(missing_runway),
      runway_surfaces_package());
  require(!runway_report.passed,
          "regional coverage must fail on unexplained missing active runway");
  require(has_issue_code(runway_report, "airport.coverage.runway.missing"),
          "missing regional runway must be reported");

  const std::string missing_slz = R"({
      "aerodromeId": "FPSZ",
      "name": "Missing regional SLZ",
      "classification": "slz_field",
      "operationalStatus": "active",
      "requiredRunways": [
        {"id": "FPSZ-RWY-01-19", "designator": "01/19"}
      ]
    })";
  flying::data_pipeline::ValidationReport slz_report =
    flying::data_pipeline::validate_regional_airport_coverage_text(
      production_approved_airport_database(),
      regional_master_list(missing_slz),
      runway_surfaces_package());
  require(!slz_report.passed,
          "regional coverage must fail on unexplained missing active SLZ area");
  require(has_issue_code(slz_report, "airport.coverage.slz_area.missing"),
          "missing regional SLZ must be reported");
}

void regional_coverage_excludes_unverified_records_and_requires_surfaces() {
  const flying::data_pipeline::ValidationReport unverified_report =
    flying::data_pipeline::validate_regional_airport_coverage_text(
      read_file(repo_root() / "data_pipeline" / "seeds" /
                "pilot-airport-master-list.json"),
      regional_master_list(),
      runway_surfaces_package());
  require(!unverified_report.passed,
          "unverified airport and runway records must not count as validated coverage");
  require(has_issue_code(unverified_report, "airport.coverage.aerodrome.unverified"),
          "unverified airport record must be reported");
  require(has_issue_code(unverified_report, "airport.coverage.slz_area.unverified"),
          "unverified SLZ record must be reported");

  const flying::data_pipeline::ValidationReport surface_report =
    flying::data_pipeline::validate_regional_airport_coverage_text(
      production_approved_airport_database(),
      regional_master_list(),
      runway_surfaces_without_slz());
  require(!surface_report.passed,
          "validated regional runways must link to a runway surface package entry");
  require(has_issue_code(surface_report, "airport.coverage.runway_surface.missing"),
          "missing runway surface linkage must be reported");
}

} // namespace

int main() {
  try {
    schema_represents_airport_master_list_and_runways();
    regional_master_list_schema_matches_runtime_required_runways_rule();
    pilot_seed_validates_and_includes_paved_and_grass_runways();
    production_validated_runways_require_thresholds_and_provenance();
    closed_aerodromes_cannot_be_default_start_locations();
    regional_coverage_accepts_configured_master_list_and_surface_links();
    regional_coverage_fails_on_missing_runways_and_slz_areas();
    regional_coverage_excludes_unverified_records_and_requires_surfaces();
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }
  return 0;
}
