#include "flying/data_pipeline/runway_importer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::filesystem::path repo_root() {
  return std::filesystem::path{FLYING_REPO_SOURCE_DIR};
}

std::filesystem::path make_temp_root() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("flying-runway-importer-test-" + std::to_string(stamp));
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << content;
}

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string{message});
  }
}

std::vector<double> numbers_after(std::string_view text, std::string_view needle) {
  std::vector<double> values;
  std::size_t offset = 0U;
  while (true) {
    const std::size_t position = text.find(needle, offset);
    if (position == std::string_view::npos) {
      return values;
    }
    const std::size_t begin = position + needle.size();
    std::size_t parsed = 0U;
    values.push_back(std::stod(std::string{text.substr(begin)}, &parsed));
    offset = begin + parsed;
  }
}

double number_after(std::string_view text, std::string_view needle) {
  const std::vector<double> values = numbers_after(text, needle);
  require(!values.empty(), "expected numeric JSON field not found");
  return values.front();
}

void require_field(std::string_view text, std::string_view needle) {
  require(text.find(needle) != std::string_view::npos,
          std::string{"expected JSON field "} + std::string{needle});
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

void require_non_flat_surface(std::string_view surface_json) {
  const std::vector<double> up_values = numbers_after(surface_json, "\"upM\":");
  require(up_values.size() >= 4U, "surface JSON must include local ENU vertex heights");
  double min_value = up_values.front();
  double max_value = up_values.front();
  for (const double value : up_values) {
    min_value = std::min(min_value, value);
    max_value = std::max(max_value, value);
  }
  require(max_value - min_value > 0.10,
          "runway surface vertices must preserve longitudinal or transverse slope");
}

std::string remove_first_paved_threshold_position(std::string text) {
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

std::string add_second_paved_runway_with_coincident_thresholds(std::string text) {
  const std::size_t runway_id = text.find("\"id\": \"FPPV-RWY-09-27\"");
  require(runway_id != std::string::npos, "paved runway fixture not found");
  const std::size_t runway_begin = text.rfind("        {", runway_id);
  require(runway_begin != std::string::npos, "paved runway object start not found");

  int depth = 0;
  bool in_string = false;
  bool escaping = false;
  std::size_t runway_end = std::string::npos;
  for (std::size_t i = runway_begin; i < text.size(); ++i) {
    const char ch = text[i];
    if (escaping) {
      escaping = false;
      continue;
    }
    if (ch == '\\' && in_string) {
      escaping = true;
      continue;
    }
    if (ch == '"') {
      in_string = !in_string;
      continue;
    }
    if (in_string) {
      continue;
    }
    if (ch == '{') {
      ++depth;
    } else if (ch == '}') {
      --depth;
      if (depth == 0) {
        runway_end = i + 1U;
        break;
      }
    }
  }
  require(runway_end != std::string::npos, "paved runway object end not found");

  std::string duplicate = text.substr(runway_begin, runway_end - runway_begin);
  duplicate = replace_all(duplicate, "FPPV-RWY-09-27", "FPPV-RWY-10-28");
  duplicate = replace_all(duplicate, "FPPV-RWY-09", "FPPV-RWY-10");
  duplicate = replace_all(duplicate, "FPPV-RWY-27", "FPPV-RWY-28");
  duplicate = replace_all(duplicate, "\"designator\": \"09/27\"", "\"designator\": \"10/28\"");
  duplicate = replace_all(duplicate, "\"designator\": \"09\"", "\"designator\": \"10\"");
  duplicate = replace_all(duplicate, "\"designator\": \"27\"", "\"designator\": \"28\"");
  duplicate = replace_all(duplicate, "\"lonDeg\": 14.5058", "\"lonDeg\": 14.4942");
  duplicate = replace_all(duplicate, "\"elevationM\": 430.9", "\"elevationM\": 429.2");
  text.insert(runway_end, ",\n" + duplicate);
  return text;
}

flying::data_pipeline::RunwayImportOptions make_options(
  const std::filesystem::path& root,
  const std::filesystem::path& airport_database) {
  flying::data_pipeline::RunwayImportOptions options;
  options.airport_database_path = airport_database;
  options.output_directory = root / "out";
  options.report_path = root / "reports" / "pilot-runway-coverage.json";
  options.package_name = "Pilot Runway Surfaces";
  options.package_version = "2026.08.0";
  return options;
}

} // namespace

int main() {
  namespace pipeline = flying::data_pipeline;

  const std::filesystem::path seed_path =
    repo_root() / "data_pipeline" / "seeds" / "pilot-airport-master-list.json";
  const std::filesystem::path root = make_temp_root();
  const pipeline::RunwayImportOptions options = make_options(root, seed_path);

  const pipeline::RunwayImportResult result = pipeline::import_pilot_runways(options);
  require(result.created(), "pilot runway import must create a package");
  require(std::filesystem::exists(root / "out" / "runway-surfaces-package.json"),
          "runway package manifest must be written");
  require(std::filesystem::exists(root / "out" / "runways" / "FPPV" /
                                  "FPPV-RWY-09-27.surface.json"),
          "paved pilot runway surface must be written");
  require(std::filesystem::exists(root / "out" / "runways" / "FPGS" /
                                  "FPGS-RWY-16-34.surface.json"),
          "grass pilot runway surface must be written");

  const std::string package_manifest =
    read_file(root / "out" / "runway-surfaces-package.json");
  require_field(package_manifest,
                "\"schemaVersion\": \"flying.pilot-runway-surfaces-package.v1\"");
  require_field(package_manifest, "\"surfaceType\":\"paved\"");
  require_field(package_manifest, "\"surfaceType\":\"grass\"");
  require_field(package_manifest, "\"coordinateFrame\": \"airport-local-ENU-per-aerodrome\"");
  require(number_after(package_manifest, "\"runwayOverridePriority\": ") >
            number_after(package_manifest, "\"genericTerrainPriority\": "),
          "runway collision priority must outrank generic terrain");
  require(number_after(package_manifest, "\"wheelContactToleranceM\": ") <= 0.05,
          "wheel-contact collision tolerance must not exceed 0.05 m");

  const std::string paved_surface =
    read_file(root / "out" / "runways" / "FPPV" / "FPPV-RWY-09-27.surface.json");
  const std::string grass_surface =
    read_file(root / "out" / "runways" / "FPGS" / "FPGS-RWY-16-34.surface.json");
  for (const std::string& surface : {paved_surface, grass_surface}) {
    require_field(surface, "\"method\": \"physical_threshold_coordinates\"");
    require_field(surface, "\"forbiddenFallback\": \"arp_plus_runway_name_and_length\"");
    require_field(surface, "\"taxiConnections\"");
    require_field(surface, "\"startPositions\"");
    require_field(surface, "\"markings\"");
    require_field(surface, "\"terrainTransition\"");
    require_field(surface, "\"lods\"");
    require_field(surface, "\"authority\": \"runway_override\"");
    require(number_after(surface, "\"maxVisualCollisionDeltaM\": ") <= 0.05,
            "collision surface must match visual surface inside wheel-contact zones");
    require_non_flat_surface(surface);
  }
  require_field(paved_surface, "\"type\":\"centerline_dashes\"");
  require_field(grass_surface, "\"type\":\"unmarked_surface\"");

  const std::string coverage_report = read_file(options.report_path);
  require_field(coverage_report,
                "\"schemaVersion\": \"flying.pilot-runway-coverage-report.v1\"");
  require_field(coverage_report, "\"passed\": true");
  require_field(coverage_report,
                "\"comparison\": \"generated_runway_surfaces_vs_approved_master_list\"");
  require_field(coverage_report, "\"missingActiveValidatedRunways\": 0");
  require_field(coverage_report, "\"pilotAirportCount\": 2");
  require_field(coverage_report, "\"runwaySurfaceCount\": 2");
  for (const std::string_view check :
       {"\"coordinate\"", "\"heading\"", "\"dimension\"", "\"ortofotoAlignment\"",
        "\"terrainTransition\"", "\"provenance\""}) {
    require_field(coverage_report, check);
  }

  std::filesystem::remove_all(root);

  const std::filesystem::path national_root = make_temp_root();
  const std::filesystem::path national_seed = national_root / "national-airport-master-list.json";
  std::string national_seed_text =
    replace_all(read_file(seed_path), "\"scope\": \"pilot_airports_only\"",
                "\"scope\": \"national_airport_master_list\"");
  national_seed_text =
    replace_all(national_seed_text, "\"status\": \"derived\"",
                "\"status\": \"production_validated\"");
  national_seed_text =
    replace_all(national_seed_text, "\"status\": \"unverified\"",
                "\"status\": \"reviewer_approved\"");
  write_file(national_seed, national_seed_text);
  const pipeline::RunwayImportOptions national_options =
    make_options(national_root, national_seed);
  const pipeline::RunwayImportResult national_result =
    pipeline::import_pilot_runways(national_options);
  require(national_result.created(),
          "national runway import must accept approved master-list scope");
  const std::string national_report = read_file(national_options.report_path);
  require_field(national_report, "\"scope\": \"national_airport_master_list\"");
  require_field(national_report, "\"activeValidatedRunwayCount\": 2");
  require_field(national_report, "\"missingActiveValidatedRunways\": 0");
  std::filesystem::remove_all(national_root);

  const std::filesystem::path missing_runway_root = make_temp_root();
  const std::filesystem::path missing_runway_seed =
    missing_runway_root / "national-airport-master-list-missing-runway.json";
  write_file(missing_runway_seed,
             add_second_paved_runway_with_coincident_thresholds(national_seed_text));
  const pipeline::RunwayImportOptions missing_runway_options =
    make_options(missing_runway_root, missing_runway_seed);
  const pipeline::RunwayImportResult missing_runway_result =
    pipeline::import_pilot_runways(missing_runway_options);
  require(!missing_runway_result.created(),
          "national runway import must fail when one same-aerodrome production runway is missing");
  const std::string missing_runway_report = read_file(missing_runway_options.report_path);
  require_field(missing_runway_report, "\"missingActiveValidatedRunways\": 1");
  require_field(missing_runway_report, "\"FPPV/FPPV-RWY-10-28\"");
  require_field(missing_runway_report,
                "\"sourceId\": \"FPPV/FPPV-RWY-10-28\"");
  std::filesystem::remove_all(missing_runway_root);

  const std::filesystem::path overwrite_root = make_temp_root();
  const pipeline::RunwayImportOptions overwrite_options =
    make_options(overwrite_root, seed_path);
  write_file(overwrite_root / "out" / "runways" / "FPGS" /
               "FPGS-RWY-16-34.surface.json",
             "{\n  \"manualGeometryReviewLock\": true\n}\n");
  const pipeline::RunwayImportResult overwrite_result =
    pipeline::import_pilot_runways(overwrite_options);
  require(!overwrite_result.created(),
          "runway importer must reject automatic overwrite of manually verified geometry");
  require(read_file(overwrite_options.report_path)
            .find("runway_import.runway.manual_geometry_overwrite_blocked") !=
          std::string::npos,
          "manual geometry overwrite must be reported");
  require(!std::filesystem::exists(overwrite_root / "out" / "runways" / "FPPV" /
                                   "FPPV-RWY-09-27.surface.json"),
          "runway importer must preflight locks before writing any surface files");
  std::filesystem::remove_all(overwrite_root);

  const std::filesystem::path bad_root = make_temp_root();
  const std::filesystem::path bad_seed = bad_root / "airport-without-threshold-position.json";
  write_file(bad_seed, remove_first_paved_threshold_position(read_file(seed_path)));
  const pipeline::RunwayImportOptions bad_options = make_options(bad_root, bad_seed);
  const pipeline::RunwayImportResult bad_result =
    pipeline::import_pilot_runways(bad_options);
  require(!bad_result.created(),
          "runway importer must reject geometry without physical threshold coordinates");
  require(!std::filesystem::exists(bad_root / "out" / "runway-surfaces-package.json"),
          "failed threshold import must not write a package manifest");
  const std::string bad_report = read_file(bad_options.report_path);
  require(bad_report.find("physicalThreshold.positionWgs84") != std::string::npos ||
            bad_report.find("physical_threshold.position") != std::string::npos,
          "missing physical threshold coordinates must be reported");
  std::filesystem::remove_all(bad_root);

  return 0;
}
