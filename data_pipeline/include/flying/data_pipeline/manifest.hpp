#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace flying::data_pipeline {

struct ChecksumDeclaration {
  std::string path;
  std::string algorithm;
  std::string value;
};

struct SourceDataset {
  std::string id;
  std::string dataset_name;
  std::string version;
  ChecksumDeclaration checksum;
  std::string license_json;
  std::string attribution_json;
  std::string coordinate_reference_system_json;
  std::string permitted_use_json;
  std::string provenance_json;
};

struct SourceManifest {
  std::string schema_version;
  std::string manifest_version;
  std::string transform_config_json;
  std::vector<SourceDataset> sources;
};

struct ValidationIssue {
  std::string severity;
  std::string code;
  std::string message;
  std::string source_id;
};

struct SourceValidationRecord {
  std::string source_id;
  std::string path;
  std::string checksum_algorithm;
  std::string declared_checksum;
  std::string computed_checksum;
  bool checksum_verified = false;
};

struct ValidationReport {
  std::string schema_version = "flying.validation-report.v1";
  bool passed = false;
  std::string source_manifest_path;
  std::string package_manifest_path;
  std::string package_id;
  std::vector<ValidationIssue> issues;
  std::vector<SourceValidationRecord> sources;
};

struct ValidationResult {
  ValidationReport report;
  std::optional<SourceManifest> manifest;

  [[nodiscard]] bool accepted() const noexcept { return report.passed && manifest.has_value(); }
};

struct PackageManifest {
  std::string schema_version = "flying.package-manifest.v1";
  std::string package_name;
  std::string package_version;
  std::string package_id;
  std::string content_hash;
  std::string source_manifest_version;
  std::string json;
};

struct PackageResult {
  ValidationReport report;
  std::optional<PackageManifest> package_manifest;

  [[nodiscard]] bool created() const noexcept {
    return report.passed && package_manifest.has_value();
  }
};

struct ValidateOptions {
  std::filesystem::path source_manifest_path;
  std::filesystem::path source_root;
  std::filesystem::path report_path;
  bool verify_checksums = true;
};

struct PackageOptions {
  std::filesystem::path source_manifest_path;
  std::filesystem::path source_root;
  std::filesystem::path package_manifest_path;
  std::filesystem::path report_path;
  std::string package_name = "flying-gis-package";
  std::string package_version;
  bool verify_checksums = true;
};

ValidationResult validate_source_manifest(const ValidateOptions& options);
PackageResult create_package_manifest(const PackageOptions& options);

} // namespace flying::data_pipeline
