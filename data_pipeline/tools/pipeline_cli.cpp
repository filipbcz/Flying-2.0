#include "flying/data_pipeline/dmr5g_terrain.hpp"
#include "flying/data_pipeline/manifest.hpp"
#include "flying/data_pipeline/pilot_region_packages.hpp"
#include "flying/data_pipeline/runway_importer.hpp"

#include <filesystem>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_usage(std::ostream& output) {
  output
    << "Usage:\n"
    << "  flying-data-pipeline validate --source-manifest PATH --report PATH "
       "[--source-root DIR]\n"
    << "  flying-data-pipeline package --source-manifest PATH --package-version VERSION "
       "--output PATH --report PATH [--source-root DIR] [--package-name NAME]\n"
    << "  flying-data-pipeline dmr5g-pilot-terrain --source-manifest PATH "
       "--terrain-config PATH --package-version VERSION --output-dir DIR --report PATH "
       "[--source-root DIR] [--package-name NAME]\n";
  output
    << "  flying-data-pipeline dmr5g-czech-republic-terrain --source-manifest PATH "
       "--terrain-config PATH --package-version VERSION --output-dir DIR --report PATH "
       "[--source-root DIR] [--package-name NAME]\n";
  output
    << "  flying-data-pipeline pilot-region-packages --source-manifest PATH "
       "--package-config PATH --package-version VERSION --output-dir DIR --report PATH "
       "[--source-root DIR] [--package-name NAME]\n";
  output
    << "  flying-data-pipeline czech-republic-packages --source-manifest PATH "
       "--package-config PATH --package-version VERSION --output-dir DIR --report PATH "
       "[--source-root DIR] [--package-name NAME]\n";
  output
    << "  flying-data-pipeline runway-import --airport-database PATH "
       "--package-version VERSION --output-dir DIR --report PATH [--package-name NAME]\n";
}

struct ParsedArgs {
  std::string command;
  std::filesystem::path source_manifest;
  std::filesystem::path source_root;
  std::filesystem::path report;
  std::filesystem::path output;
  std::filesystem::path output_dir;
  std::filesystem::path terrain_config;
  std::filesystem::path package_config;
  std::filesystem::path airport_database;
  std::string package_name = "flying-gis-package";
  std::string package_version;
};

std::optional<std::string> take_value(const std::vector<std::string_view>& args,
                                      std::size_t& index,
                                      std::string_view flag) {
  if (index + 1U >= args.size()) {
    return std::nullopt;
  }
  ++index;
  if (args[index].empty() || args[index].front() == '-') {
    std::cerr << "Missing value for " << flag << "\n";
    return std::nullopt;
  }
  return std::string{args[index]};
}

std::optional<ParsedArgs> parse_args(int argc, char** argv) {
  std::vector<std::string_view> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }

  if (args.size() < 2U || args[1] == "--help" || args[1] == "-h") {
    print_usage(std::cout);
    return std::nullopt;
  }

  ParsedArgs parsed;
  parsed.command = std::string{args[1]};
  if (parsed.command != "validate" && parsed.command != "package" &&
      parsed.command != "dmr5g-pilot-terrain" &&
      parsed.command != "dmr5g-czech-republic-terrain" &&
      parsed.command != "pilot-region-packages" &&
      parsed.command != "czech-republic-packages" &&
      parsed.command != "runway-import") {
    std::cerr << "Unknown command: " << parsed.command << "\n";
    return std::nullopt;
  }

  for (std::size_t i = 2U; i < args.size(); ++i) {
    const std::string_view flag = args[i];
    std::optional<std::string> value;
    if (flag == "--source-manifest") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.source_manifest = *value;
    } else if (flag == "--source-root") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.source_root = *value;
    } else if (flag == "--report") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.report = *value;
    } else if (flag == "--output") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.output = *value;
    } else if (flag == "--output-dir") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.output_dir = *value;
    } else if (flag == "--terrain-config") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.terrain_config = *value;
    } else if (flag == "--package-config") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.package_config = *value;
    } else if (flag == "--airport-database") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.airport_database = *value;
    } else if (flag == "--package-name") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.package_name = *value;
    } else if (flag == "--package-version") {
      value = take_value(args, i, flag);
      if (!value.has_value()) {
        return std::nullopt;
      }
      parsed.package_version = *value;
    } else {
      std::cerr << "Unknown option: " << flag << "\n";
      return std::nullopt;
    }
  }

  if (parsed.command != "runway-import" && parsed.source_manifest.empty()) {
    std::cerr << "--source-manifest is required\n";
    return std::nullopt;
  }
  if (parsed.report.empty()) {
    std::cerr << "--report is required\n";
    return std::nullopt;
  }
  if (parsed.command == "package" || parsed.command == "dmr5g-pilot-terrain" ||
      parsed.command == "dmr5g-czech-republic-terrain" ||
      parsed.command == "pilot-region-packages" ||
      parsed.command == "czech-republic-packages" || parsed.command == "runway-import") {
    if (parsed.package_version.empty()) {
      std::cerr << "--package-version is required for " << parsed.command << "\n";
      return std::nullopt;
    }
  }
  if (parsed.command == "package") {
    if (parsed.output.empty()) {
      std::cerr << "--output is required for package\n";
      return std::nullopt;
    }
  }
  if (parsed.command == "dmr5g-pilot-terrain" ||
      parsed.command == "dmr5g-czech-republic-terrain") {
    if (parsed.package_name == "flying-gis-package") {
      parsed.package_name = parsed.command == "dmr5g-czech-republic-terrain"
                              ? "flying-dmr5g-czech-republic-terrain"
                              : "flying-dmr5g-pilot-terrain";
    }
    if (parsed.terrain_config.empty()) {
      std::cerr << "--terrain-config is required for " << parsed.command << "\n";
      return std::nullopt;
    }
    if (parsed.output_dir.empty()) {
      std::cerr << "--output-dir is required for " << parsed.command << "\n";
      return std::nullopt;
    }
  }
  if (parsed.command == "pilot-region-packages" ||
      parsed.command == "czech-republic-packages") {
    if (parsed.package_name == "flying-gis-package") {
      parsed.package_name = parsed.command == "czech-republic-packages"
                              ? "flying-czech-republic-offline-gis"
                              : "flying-pilot-region-offline-gis";
    }
    if (parsed.package_config.empty()) {
      std::cerr << "--package-config is required for " << parsed.command << "\n";
      return std::nullopt;
    }
    if (parsed.output_dir.empty()) {
      std::cerr << "--output-dir is required for " << parsed.command << "\n";
      return std::nullopt;
    }
  }
  if (parsed.command == "runway-import") {
    if (parsed.package_name == "flying-gis-package") {
      parsed.package_name = "flying-pilot-runway-surfaces";
    }
    if (parsed.airport_database.empty()) {
      std::cerr << "--airport-database is required for runway-import\n";
      return std::nullopt;
    }
    if (parsed.output_dir.empty()) {
      std::cerr << "--output-dir is required for runway-import\n";
      return std::nullopt;
    }
  }
  return parsed;
}

} // namespace

int main(int argc, char** argv) {
  try {
    std::optional<ParsedArgs> parsed = parse_args(argc, argv);
    if (!parsed.has_value()) {
      return (argc >= 2 && (std::string_view{argv[1]} == "--help" ||
                            std::string_view{argv[1]} == "-h"))
               ? 0
               : 64;
    }

    if (parsed->command == "validate") {
      flying::data_pipeline::ValidateOptions options;
      options.source_manifest_path = parsed->source_manifest;
      options.source_root = parsed->source_root;
      options.report_path = parsed->report;
      const flying::data_pipeline::ValidationResult result =
        flying::data_pipeline::validate_source_manifest(options);
      if (!result.accepted()) {
        std::cerr << "Source manifest validation failed; report written to "
                  << parsed->report << "\n";
        return 2;
      }
      std::cout << "Source manifest validation passed; report written to "
                << parsed->report << "\n";
      return 0;
    }

    if (parsed->command == "dmr5g-pilot-terrain" ||
        parsed->command == "dmr5g-czech-republic-terrain") {
      flying::data_pipeline::Dmr5gPilotTerrainOptions options;
      options.source_manifest_path = parsed->source_manifest;
      options.source_root = parsed->source_root;
      options.terrain_config_path = parsed->terrain_config;
      options.output_directory = parsed->output_dir;
      options.report_path = parsed->report;
      options.package_name = parsed->package_name;
      options.package_version = parsed->package_version;
      const flying::data_pipeline::Dmr5gPilotTerrainResult result =
        parsed->command == "dmr5g-czech-republic-terrain"
          ? flying::data_pipeline::process_dmr5g_czech_republic_terrain(options)
          : flying::data_pipeline::process_dmr5g_pilot_terrain(options);
      if (!result.created()) {
        std::cerr << "DMR 5G terrain processing failed; report written to "
                  << parsed->report << "\n";
        return 2;
      }
      std::cout << "DMR 5G terrain package " << result.report.package_id
                << " written to " << result.package_manifest_path << "\n";
      return 0;
    }

    if (parsed->command == "pilot-region-packages" ||
        parsed->command == "czech-republic-packages") {
      flying::data_pipeline::PilotRegionPackageOptions options;
      options.source_manifest_path = parsed->source_manifest;
      options.source_root = parsed->source_root;
      options.package_config_path = parsed->package_config;
      options.output_directory = parsed->output_dir;
      options.report_path = parsed->report;
      options.package_name = parsed->package_name;
      options.package_version = parsed->package_version;
      const flying::data_pipeline::PilotRegionPackageResult result =
        parsed->command == "czech-republic-packages"
          ? flying::data_pipeline::process_czech_republic_packages(options)
          : flying::data_pipeline::process_pilot_region_packages(options);
      if (!result.created()) {
        std::cerr << "Offline GIS package processing failed; report written to "
                  << parsed->report << "\n";
        return 2;
      }
      std::cout << "Offline GIS package " << result.report.package_id
                << " written to " << result.package_manifest_path << "\n";
      return 0;
    }

    if (parsed->command == "runway-import") {
      flying::data_pipeline::RunwayImportOptions options;
      options.airport_database_path = parsed->airport_database;
      options.output_directory = parsed->output_dir;
      options.report_path = parsed->report;
      options.package_name = parsed->package_name;
      options.package_version = parsed->package_version;
      const flying::data_pipeline::RunwayImportResult result =
        flying::data_pipeline::import_pilot_runways(options);
      if (!result.created()) {
        std::cerr << "Pilot runway import failed; report written to "
                  << parsed->report << "\n";
        return 2;
      }
      std::cout << "Pilot runway surface package " << result.report.package_id
                << " written to " << result.package_manifest_path << "\n";
      return 0;
    }

    flying::data_pipeline::PackageOptions options;
    options.source_manifest_path = parsed->source_manifest;
    options.source_root = parsed->source_root;
    options.report_path = parsed->report;
    options.package_manifest_path = parsed->output;
    options.package_name = parsed->package_name;
    options.package_version = parsed->package_version;
    const flying::data_pipeline::PackageResult result =
      flying::data_pipeline::create_package_manifest(options);
    if (!result.created()) {
      std::cerr << "Package manifest generation failed; report written to "
                << parsed->report << "\n";
      return 2;
    }
    std::cout << "Package manifest " << result.package_manifest->package_id
              << " written to " << parsed->output << "\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "flying-data-pipeline: " << error.what() << "\n";
    return 1;
  }
}
