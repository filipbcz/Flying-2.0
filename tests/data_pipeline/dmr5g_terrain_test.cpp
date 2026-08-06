#include "flying/data_pipeline/dmr5g_terrain.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kChecksumFixture =
  "0000000000000000000000000000000000000000000000000000000000000000";

constexpr std::string_view kWestTile =
  "0 0 100.00\n"
  "1 0 100.50\n"
  "2 0 101.00\n"
  "0 1 102.00\n"
  "1 1 102.50\n"
  "2 1 103.00\n"
  "0 2 104.00\n"
  "1 2 104.50\n"
  "2 2 105.00\n";

constexpr std::string_view kEastTile =
  "2 0 101.04\n"
  "3 0 101.50\n"
  "4 0 102.00\n"
  "2 1 103.04\n"
  "3 1 103.50\n"
  "4 1 104.00\n"
  "2 2 105.04\n"
  "3 2 105.50\n"
  "4 2 106.00\n";

constexpr std::string_view kEastTileMisalignedNorth =
  "2 0.5 101.04\n"
  "3 0.5 101.50\n"
  "4 0.5 102.00\n"
  "2 1.5 103.04\n"
  "3 1.5 103.50\n"
  "4 1.5 104.00\n"
  "2 2.5 105.04\n"
  "3 2.5 105.50\n"
  "4 2.5 106.00\n";

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

std::string replace_once(std::string text,
                         std::string_view needle,
                         std::string_view replacement) {
  const std::size_t position = text.find(needle);
  assert(position != std::string::npos);
  text.replace(position, needle.size(), replacement);
  return text;
}

std::filesystem::path make_temp_root() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("flying-dmr5g-terrain-test-" + std::to_string(stamp));
}

flying::data_pipeline::Dmr5gPilotTerrainOptions make_options(
  const std::filesystem::path& root) {
  flying::data_pipeline::Dmr5gPilotTerrainOptions options;
  options.source_manifest_path = root / "source-manifest.json";
  options.source_root = root;
  options.terrain_config_path = root / "terrain-config.json";
  options.output_directory = root / "out";
  options.report_path = root / "reports" / "terrain-validation.json";
  options.package_name = "DMR 5G Pilot Terrain";
  options.package_version = "2026.08.0";
  options.verify_checksums = false;
  return options;
}

std::string make_source_entry(std::string_view id, std::string_view path) {
  std::ostringstream output;
  output
    << "    {\n"
    << "      \"id\": \"" << id << "\",\n"
    << "      \"datasetName\": \"DMR 5G Pilot Fixture\",\n"
    << "      \"version\": \"2026.08.fixture\",\n"
    << "      \"license\": {\"name\": \"Fixture Terrain License\"},\n"
    << "      \"attribution\": {\"text\": \"Contains DMR 5G pilot fixture terrain.\"},\n"
    << "      \"coordinateReferenceSystem\": {\"authority\": \"EPSG\", \"code\": \"5514\", \"name\": \"S-JTSK / Krovak East North\"},\n"
    << "      \"permittedUse\": {\"terrainDerivatives\": true, \"runtimeRedistribution\": true, \"attributionRequired\": true},\n"
    << "      \"provenance\": {\"publisher\": \"Fixture Publisher\", \"sourceUrl\": \"https://example.invalid/dmr5g\", \"retrievedAtUtc\": \"2026-08-02T00:00:00Z\"},\n"
    << "      \"checksum\": {\"path\": \"" << path
    << "\", \"algorithm\": \"sha256\", \"value\": \"" << kChecksumFixture << "\"}\n"
    << "    }";
  return output.str();
}

std::string make_source_manifest() {
  std::ostringstream output;
  output
    << "{\n"
    << "  \"schemaVersion\": \"flying.source-manifest.v1\",\n"
    << "  \"manifestVersion\": \"2026.08.dmr5g-fixture\",\n"
    << "  \"transform\": {\n"
    << "    \"targetCrs\": {\"authority\": \"FLYING\", \"code\": \"PILOT-ENU\", \"name\": \"Flying pilot local ENU\"},\n"
    << "    \"steps\": [{\"name\": \"dmr5g-pilot\", \"operation\": \"terrain-package\"}]\n"
    << "  },\n"
    << "  \"sources\": [\n"
    << make_source_entry("cuzk-dmr5g-fixture-west", "dmr5g/west.xyz") << ",\n"
    << make_source_entry("cuzk-dmr5g-fixture-east", "dmr5g/east.xyz") << "\n"
    << "  ]\n"
    << "}\n";
  return output.str();
}

std::string make_terrain_config() {
  return R"({
  "schemaVersion": "flying.dmr5g-pilot-terrain-config.v1",
  "packageIdHint": "dmr5g-pilot-fixture",
  "pilotRegion": {
    "id": "pilot-50km-fixture",
    "minEastM": 0,
    "minNorthM": 0,
    "widthM": 50000,
    "heightM": 50000
  },
  "transform": {
    "sourceCrs": {"authority": "EPSG", "code": "5514", "name": "S-JTSK / Krovak East North"},
    "targetCrs": {"authority": "FLYING", "code": "PILOT-ENU", "name": "Flying pilot local ENU"},
    "sourceHeightSystem": {"name": "Baltic Vertical Datum - After Adjustment", "kind": "orthometric"},
    "targetHeightSystem": {"name": "WGS 84 ellipsoidal", "kind": "ellipsoidal"},
    "proj": {
      "version": "9.4.fixture",
      "pipeline": "+proj=pipeline +step +proj=axisswap +order=1,2 +step +proj=unitconvert +xy_in=m +xy_out=m"
    },
    "geoid": {
      "model": "Bpv-to-WGS84 fixture geoid",
      "grid": "fixture-bpv-to-wgs84.gtx",
      "undulationMeters": 45.25
    },
    "sourceToProject": {
      "eastFromSourceXScale": 1,
      "eastFromSourceYScale": 0,
      "eastOffsetM": 0,
      "northFromSourceXScale": 0,
      "northFromSourceYScale": 1,
      "northOffsetM": 0
    },
    "heightTransform": {
      "sourceHeightScale": 1,
      "orthometricOffsetM": 0
    }
  },
  "sourceTiles": [
    {"id": "west", "path": "dmr5g/west.xyz", "format": "xyz", "declaredVerticalErrorM": 0.18},
    {"id": "east", "path": "dmr5g/east.xyz", "format": "xyz", "declaredVerticalErrorM": 0.18}
  ],
  "renderLods": [
    {"level": 0, "sampleStride": 1},
    {"level": 1, "sampleStride": 2}
  ],
  "activeAircraftZone": {
    "minEastM": 0,
    "maxEastM": 4,
    "minNorthM": 0,
    "maxNorthM": 2,
    "sampleStride": 1
  },
  "controlPoints": [
    {"id": "west-center", "sourceX": 1, "sourceY": 1, "expectedOrthometricHeightM": 102.5, "sourceVerticalErrorM": 0.18},
    {"id": "east-center", "sourceX": 3, "sourceY": 1, "expectedOrthometricHeightM": 103.5, "sourceVerticalErrorM": 0.18}
  ],
  "edgeToleranceM": 0.000001,
  "boundaryCleanMaxAdjustmentM": 0.10
}
)";
}

std::string make_misaligned_collision_zone_config() {
  return replace_once(make_terrain_config(),
                      R"(  "activeAircraftZone": {
    "minEastM": 0,
    "maxEastM": 4,
    "minNorthM": 0,
    "maxNorthM": 2,
    "sampleStride": 1
  },)",
                      R"(  "activeAircraftZone": {
    "minEastM": 0.25,
    "maxEastM": 3.75,
    "minNorthM": 0.25,
    "maxNorthM": 1.75,
    "sampleStride": 2
  },)");
}

std::string make_misaligned_edge_config() {
  return replace_once(make_terrain_config(),
                      R"(    {"id": "west-center", "sourceX": 1, "sourceY": 1, "expectedOrthometricHeightM": 102.5, "sourceVerticalErrorM": 0.18},
    {"id": "east-center", "sourceX": 3, "sourceY": 1, "expectedOrthometricHeightM": 103.5, "sourceVerticalErrorM": 0.18})",
                      R"(    {"id": "west-center", "sourceX": 1, "sourceY": 1, "expectedOrthometricHeightM": 102.5, "sourceVerticalErrorM": 0.18})");
}

std::string make_unmanifested_tile_config() {
  return replace_once(make_terrain_config(),
                      R"({"id": "east", "path": "dmr5g/east.xyz", "format": "xyz", "declaredVerticalErrorM": 0.18})",
                      R"({"id": "east", "path": "dmr5g/unlisted.xyz", "format": "xyz", "declaredVerticalErrorM": 0.18})");
}

std::string make_czech_republic_terrain_config() {
  std::string config = replace_once(make_terrain_config(),
                                    "\"schemaVersion\": \"flying.dmr5g-pilot-terrain-config.v1\"",
                                    "\"schemaVersion\": \"flying.dmr5g-czech-republic-terrain-config.v1\",\n  \"coverageScope\": \"czech-republic\"");
  config = replace_once(config, "\"packageIdHint\": \"dmr5g-pilot-fixture\"", "\"packageIdHint\": \"dmr5g-czech-republic-fixture\"");
  config = replace_once(config, "\"id\": \"pilot-50km-fixture\"", "\"id\": \"czech-republic-fixture\"");
  config = replace_once(config, "\"widthM\": 50000", "\"widthM\": 430000");
  config = replace_once(config, "\"heightM\": 50000", "\"heightM\": 280000");
  return config;
}

void write_fixture_inputs(const std::filesystem::path& root,
                          std::string_view east_tile,
                          std::string_view terrain_config) {
  write_file(root / "dmr5g" / "west.xyz", kWestTile);
  write_file(root / "dmr5g" / "east.xyz", east_tile);
  write_file(root / "source-manifest.json", make_source_manifest());
  write_file(root / "terrain-config.json", terrain_config);
}

} // namespace

int main() {
  namespace pipeline = flying::data_pipeline;

  const std::filesystem::path root = make_temp_root();
  write_fixture_inputs(root, kEastTile, make_terrain_config());

  pipeline::Dmr5gPilotTerrainOptions options = make_options(root);

  const pipeline::Dmr5gPilotTerrainResult result =
    pipeline::process_dmr5g_pilot_terrain(options);
  assert(result.created());
  assert(std::filesystem::exists(root / "out" / "terrain-package.json"));
  assert(std::filesystem::exists(root / "out" / "render" / "lod0" / "west.terrain.csv"));
  assert(std::filesystem::exists(root / "out" / "render" / "lod1" / "east.terrain.csv"));
  assert(std::filesystem::exists(root / "out" / "collision" / "west.collision.csv"));
  assert(std::filesystem::exists(root / "out" / "collision" / "east.collision.csv"));

  const std::string package_manifest = read_file(root / "out" / "terrain-package.json");
  assert(package_manifest.find("\"schemaVersion\": \"flying.terrain-package.v1\"") !=
         std::string::npos);
  assert(package_manifest.find("\"recordedProjGeoidConfiguration\"") != std::string::npos);
  assert(package_manifest.find("\"pipeline\":\"+proj=pipeline") != std::string::npos);
  assert(package_manifest.find("\"geoid\"") != std::string::npos);
  assert(package_manifest.find("\"renderLods\"") != std::string::npos);
  assert(package_manifest.find("\"level\": 1") != std::string::npos);
  assert(package_manifest.find("\"collisionTiles\"") != std::string::npos);
  assert(package_manifest.find("\"sizeBytes\"") != std::string::npos);
  assert(package_manifest.find("\"collisionTilesAreSeparate\": true") != std::string::npos);
  assert(package_manifest.find("\"streaming\"") != std::string::npos);
  assert(package_manifest.find("\"runtimeNetworkRequired\": false") != std::string::npos);
  assert(package_manifest.find("\"packagingLayout\"") != std::string::npos);
  assert(package_manifest.find("\"collisionTileCount\": 2") != std::string::npos);
  assert(package_manifest.find("\"normalFrame\": \"project-local-ENU\"") != std::string::npos);
  assert(package_manifest.find("\"edgeCleaned\": true") != std::string::npos);
  assert(package_manifest.find("\"maxAbsStepM\": 0") != std::string::npos);
  assert(package_manifest.find("\"west-center\"") != std::string::npos);
  assert(package_manifest.find("\"east-center\"") != std::string::npos);

  const std::string report = read_file(options.report_path);
  assert(report.find("\"passed\": true") != std::string::npos);
  assert(report.find("\"controlPoints\"") != std::string::npos);
  assert(report.find("\"allowedErrorM\": 0.28000000000000003") != std::string::npos);
  assert(report.find("\"edgeContinuity\"") != std::string::npos);
  assert(report.find("\"adjacentPairCount\": 1") != std::string::npos);
  assert(report.find("\"maxPreCleanStepM\"") != std::string::npos);

  const std::string west_lod0 = read_file(root / "out" / "render" / "lod0" / "west.terrain.csv");
  assert(west_lod0.find("normal_east,normal_north,normal_up") != std::string::npos);
  assert(west_lod0.find("1.000000,1.000000,147.750000,102.500000") !=
         std::string::npos);

  std::filesystem::remove_all(root);

  {
    const std::filesystem::path czech_root = make_temp_root();
    write_fixture_inputs(czech_root, kEastTile, make_czech_republic_terrain_config());
    pipeline::Dmr5gPilotTerrainOptions czech_options = make_options(czech_root);
    czech_options.package_name = "DMR 5G Czech Republic Terrain";
    const pipeline::Dmr5gPilotTerrainResult czech_result =
      pipeline::process_dmr5g_czech_republic_terrain(czech_options);
    assert(!czech_result.created());
    assert(!std::filesystem::exists(czech_root / "out" / "terrain-package.json"));
    const std::string czech_report = read_file(czech_options.report_path);
    assert(czech_report.find(
             "\"code\": \"terrain_config.czechRepublic.sourceTiles.coverage_incomplete\"") !=
           std::string::npos);
    std::filesystem::remove_all(czech_root);
  }

  {
    const std::filesystem::path czech_command_root = make_temp_root();
    write_fixture_inputs(czech_command_root, kEastTile, make_terrain_config());
    pipeline::Dmr5gPilotTerrainOptions czech_command_options =
      make_options(czech_command_root);
    czech_command_options.package_name = "DMR 5G Czech Republic Terrain";
    const pipeline::Dmr5gPilotTerrainResult czech_command_result =
      pipeline::process_dmr5g_czech_republic_terrain(czech_command_options);
    assert(!czech_command_result.created());
    const std::string czech_command_report =
      read_file(czech_command_options.report_path);
    assert(czech_command_report.find(
             "\"code\": \"terrain.options.czech_republic_config.required\"") !=
           std::string::npos);
    std::filesystem::remove_all(czech_command_root);
  }

  {
    const std::filesystem::path collision_root = make_temp_root();
    write_fixture_inputs(collision_root, kEastTile, make_misaligned_collision_zone_config());
    pipeline::Dmr5gPilotTerrainOptions collision_options = make_options(collision_root);
    const pipeline::Dmr5gPilotTerrainResult collision_result =
      pipeline::process_dmr5g_pilot_terrain(collision_options);
    assert(collision_result.created());
    assert(std::filesystem::exists(collision_root / "out" / "collision" / "west.collision.csv"));
    const std::string collision_manifest =
      read_file(collision_root / "out" / "terrain-package.json");
    assert(collision_manifest.find("\"collisionTiles\"") != std::string::npos);
    assert(collision_manifest.find("\"bounds\": {\"minEastM\": 0, \"maxEastM\": 2") !=
           std::string::npos);
    std::filesystem::remove_all(collision_root);
  }

  {
    const std::filesystem::path edge_root = make_temp_root();
    write_fixture_inputs(edge_root, kEastTileMisalignedNorth, make_misaligned_edge_config());
    pipeline::Dmr5gPilotTerrainOptions edge_options = make_options(edge_root);
    const pipeline::Dmr5gPilotTerrainResult edge_result =
      pipeline::process_dmr5g_pilot_terrain(edge_options);
    assert(!edge_result.created());
    const std::string edge_report = read_file(edge_options.report_path);
    assert(edge_report.find("\"code\": \"terrain.edge.unmatched_samples\"") !=
           std::string::npos);
    assert(edge_report.find("\"unmatchedSampleCount\": 4") != std::string::npos);
    std::filesystem::remove_all(edge_root);
  }

  {
    const std::filesystem::path lineage_root = make_temp_root();
    write_fixture_inputs(lineage_root, kEastTile, make_unmanifested_tile_config());
    write_file(lineage_root / "dmr5g" / "unlisted.xyz", kEastTile);
    pipeline::Dmr5gPilotTerrainOptions lineage_options = make_options(lineage_root);
    const pipeline::Dmr5gPilotTerrainResult lineage_result =
      pipeline::process_dmr5g_pilot_terrain(lineage_options);
    assert(!lineage_result.created());
    const std::string lineage_report = read_file(lineage_options.report_path);
    assert(lineage_report.find("\"code\": \"terrain.source_manifest.path_missing\"") !=
           std::string::npos);
    assert(!std::filesystem::exists(lineage_root / "out" / "terrain-package.json"));
    std::filesystem::remove_all(lineage_root);
  }

  return 0;
}
