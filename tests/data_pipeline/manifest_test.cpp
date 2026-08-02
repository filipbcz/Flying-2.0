#include "flying/data_pipeline/manifest.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::string_view kPayload = "Flying GIS source payload\n";
constexpr std::string_view kPayloadSha256 =
  "4d8ec90351c0b07f79141c0d7a1471e464ea3e5c135fbf5e0f7cfbdd784e3f7a";

void write_file(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << content;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string make_manifest(bool include_checksum, bool include_provenance) {
  std::vector<std::string> fields = {
    R"("id": "cuzk-dmr5g-fixture")",
    R"("datasetName": "Pipeline Fixture Source")",
    R"("version": "2026.08.fixture")",
    R"("license": {"name": "CC BY 4.0", "url": "https://creativecommons.org/licenses/by/4.0/"})",
    R"("attribution": {"text": "Contains source data for Flying pipeline validation."})",
    R"("coordinateReferenceSystem": {"authority": "EPSG", "code": "5514", "name": "S-JTSK / Krovak East North"})",
    R"("permittedUse": {"terrainDerivatives": true, "runtimeRedistribution": true, "attributionRequired": true})",
  };

  if (include_checksum) {
    fields.push_back(std::string{R"("checksum": {"path": "sources/payload.txt", "algorithm": "sha256", "value": ")"} +
                     std::string{kPayloadSha256} + R"("})");
  }
  if (include_provenance) {
    fields.push_back(
      R"("provenance": {"publisher": "Fixture Publisher", "sourceUrl": "https://example.invalid/source.zip", "retrievedAtUtc": "2026-08-02T00:00:00Z"})");
  }

  std::ostringstream source;
  source << "{\n";
  for (std::size_t i = 0; i < fields.size(); ++i) {
    source << "        " << fields[i];
    if (i + 1U != fields.size()) {
      source << ",";
    }
    source << "\n";
  }
  source << "      }";

  std::ostringstream manifest;
  manifest
    << "{\n"
    << "  \"schemaVersion\": \"flying.source-manifest.v1\",\n"
    << "  \"manifestVersion\": \"2026.08.fixture\",\n"
    << "  \"transform\": {\n"
    << "    \"targetCrs\": {\"authority\": \"EPSG\", \"code\": \"4978\", \"name\": \"WGS 84 geocentric\"},\n"
    << "    \"steps\": [{\"name\": \"metadata-only\", \"operation\": \"declare-transform\"}]\n"
    << "  },\n"
    << "  \"sources\": [\n"
    << "      " << source.str() << "\n"
    << "  ]\n"
    << "}\n";
  return manifest.str();
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

std::filesystem::path make_temp_root() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("flying-data-pipeline-test-" + std::to_string(stamp));
}

} // namespace

int main() {
  namespace pipeline = flying::data_pipeline;

  const std::filesystem::path root = make_temp_root();
  std::filesystem::create_directories(root / "sources");
  write_file(root / "sources" / "payload.txt", kPayload);

  const std::filesystem::path valid_manifest = root / "manifest.valid.json";
  write_file(valid_manifest, make_manifest(true, true));

  pipeline::ValidateOptions validate_options;
  validate_options.source_manifest_path = valid_manifest;
  validate_options.source_root = root;
  validate_options.report_path = root / "reports" / "valid.json";
  const pipeline::ValidationResult validation =
    pipeline::validate_source_manifest(validate_options);
  assert(validation.accepted());
  assert(validation.report.sources.size() == 1U);
  assert(validation.report.sources.front().checksum_verified);
  assert(std::filesystem::exists(validate_options.report_path));

  pipeline::PackageOptions package_options;
  package_options.source_manifest_path = valid_manifest;
  package_options.source_root = root;
  package_options.package_name = "Pilot Terrain";
  package_options.package_version = "2026.08.0";
  package_options.package_manifest_path = root / "packages" / "package-1.json";
  package_options.report_path = root / "reports" / "package-1.json";
  const pipeline::PackageResult package_one =
    pipeline::create_package_manifest(package_options);
  assert(package_one.created());
  assert(package_one.package_manifest->package_id.rfind("pilot-terrain-", 0U) == 0U);
  assert(read_file(package_options.package_manifest_path).find("\"sourceLineage\"") !=
         std::string::npos);

  package_options.package_manifest_path = root / "packages" / "package-2.json";
  package_options.report_path = root / "reports" / "package-2.json";
  const pipeline::PackageResult package_two =
    pipeline::create_package_manifest(package_options);
  assert(package_two.created());
  assert(package_one.package_manifest->package_id == package_two.package_manifest->package_id);
  assert(read_file(root / "packages" / "package-1.json") ==
         read_file(root / "packages" / "package-2.json"));

  const std::filesystem::path missing_provenance = root / "manifest.missing-provenance.json";
  write_file(missing_provenance, make_manifest(true, false));
  package_options.source_manifest_path = missing_provenance;
  package_options.package_manifest_path = root / "packages" / "missing-provenance.json";
  package_options.report_path = root / "reports" / "missing-provenance.json";
  const pipeline::PackageResult no_provenance =
    pipeline::create_package_manifest(package_options);
  assert(!no_provenance.created());
  assert(has_issue_code(no_provenance.report, "source.provenance.missing"));
  assert(!std::filesystem::exists(package_options.package_manifest_path));

  const std::filesystem::path missing_checksum = root / "manifest.missing-checksum.json";
  write_file(missing_checksum, make_manifest(false, true));
  validate_options.source_manifest_path = missing_checksum;
  validate_options.report_path = root / "reports" / "missing-checksum.json";
  const pipeline::ValidationResult no_checksum =
    pipeline::validate_source_manifest(validate_options);
  assert(!no_checksum.accepted());
  assert(has_issue_code(no_checksum.report, "source.checksum.missing"));
  assert(std::filesystem::exists(validate_options.report_path));

  package_options.source_manifest_path = missing_checksum;
  package_options.package_manifest_path = root / "packages" / "missing-checksum.json";
  package_options.report_path = root / "reports" / "missing-checksum-package.json";
  const pipeline::PackageResult no_checksum_package =
    pipeline::create_package_manifest(package_options);
  assert(!no_checksum_package.created());
  assert(has_issue_code(no_checksum_package.report, "source.checksum.missing"));
  assert(!std::filesystem::exists(package_options.package_manifest_path));

  std::filesystem::remove_all(root);
  return 0;
}
