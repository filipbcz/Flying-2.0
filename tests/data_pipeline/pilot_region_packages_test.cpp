#include "flying/data_pipeline/pilot_region_packages.hpp"

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

std::filesystem::path make_temp_root() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("flying-pilot-region-package-test-" + std::to_string(stamp));
}

std::string replace_once(std::string text,
                         std::string_view needle,
                         std::string_view replacement) {
  const std::size_t position = text.find(needle);
  assert(position != std::string::npos);
  text.replace(position, needle.size(), replacement);
  return text;
}

std::string source_entry(std::string_view id,
                         std::string_view dataset_name,
                         std::string_view path,
                         std::string_view attribution) {
  std::ostringstream output;
  output
    << "    {\n"
    << "      \"id\": \"" << id << "\",\n"
    << "      \"datasetName\": \"" << dataset_name << "\",\n"
    << "      \"version\": \"2026.08.fixture\",\n"
    << "      \"license\": {\"name\": \"CUZK Open Data / Geonames Fixture License\"},\n"
    << "      \"attribution\": {\"text\": \"" << attribution << "\"},\n"
    << "      \"coordinateReferenceSystem\": {\"authority\": \"EPSG\", \"code\": \"5514\", \"name\": \"S-JTSK / Krovak East North\"},\n"
    << "      \"permittedUse\": {\"terrainDerivatives\": true, \"runtimeRedistribution\": true, \"attributionRequired\": true},\n"
    << "      \"provenance\": {\"publisher\": \"Fixture Publisher\", \"sourceUrl\": \"https://example.invalid/pilot-source\", \"retrievedAtUtc\": \"2026-08-02T00:00:00Z\"},\n"
    << "      \"checksum\": {\"path\": \"" << path
    << "\", \"algorithm\": \"sha256\", \"value\": \"" << kChecksumFixture << "\"}\n"
    << "    }";
  return output.str();
}

std::string source_manifest() {
  std::ostringstream output;
  output
    << "{\n"
    << "  \"schemaVersion\": \"flying.source-manifest.v1\",\n"
    << "  \"manifestVersion\": \"2026.08.pilot-fixture\",\n"
    << "  \"transform\": {\n"
    << "    \"targetCrs\": {\"authority\": \"FLYING\", \"code\": \"PILOT-ENU\", \"name\": \"Flying pilot local ENU\"},\n"
    << "    \"steps\": [{\"name\": \"pilot-region-packages\", \"operation\": \"ortho-vector-mask-package\"}]\n"
    << "  },\n"
    << "  \"sources\": [\n"
    << source_entry("cuzk-ortofoto-fixture", "CUZK Ortofoto Fixture", "ortofoto/pilot.ppm", "Contains CUZK Ortofoto pilot fixture imagery.") << ",\n"
    << source_entry("cuzk-zabaged-roads-fixture", "CUZK ZABAGED Roads Fixture", "zabaged/roads.geojson", "Contains CUZK ZABAGED pilot roads.") << ",\n"
    << source_entry("cuzk-zabaged-rail-fixture", "CUZK ZABAGED Rail Fixture", "zabaged/rail.geojson", "Contains CUZK ZABAGED pilot rail.") << ",\n"
    << source_entry("cuzk-zabaged-water-fixture", "CUZK ZABAGED Water Fixture", "zabaged/water.geojson", "Contains CUZK ZABAGED pilot water.") << ",\n"
    << source_entry("cuzk-zabaged-settlements-fixture", "CUZK ZABAGED Settlements Fixture", "zabaged/settlements.geojson", "Contains CUZK ZABAGED pilot settlements.") << ",\n"
    << source_entry("cuzk-zabaged-vegetation-fixture", "CUZK ZABAGED Vegetation Fixture", "zabaged/vegetation.geojson", "Contains CUZK ZABAGED pilot vegetation.") << ",\n"
    << source_entry("cuzk-zabaged-notable-fixture", "CUZK ZABAGED Notable Objects Fixture", "zabaged/notable.geojson", "Contains CUZK ZABAGED pilot notable objects.") << ",\n"
    << source_entry("project-airport-fixture", "Project Airport Fixture", "airports/airport-objects.geojson", "Contains project-derived approved airport objects.") << ",\n"
    << source_entry("project-runway-fixture", "Project Runway Fixture", "runways/runway-objects.geojson", "Contains project-derived approved runway objects.") << ",\n"
    << source_entry("geonames-pilot-fixture", "Geonames Pilot Fixture", "geonames/labels.csv", "Contains Geonames pilot labels.") << ",\n"
    << source_entry("terrain-elevation-fixture", "Terrain Elevation Fixture", "packages/terrain-elevation-manifest.json", "Contains scoped DMR 5G terrain elevation package.") << ",\n"
    << source_entry("terrain-collision-fixture", "Terrain Collision Fixture", "packages/terrain-collision-manifest.json", "Contains scoped terrain collision package.") << ",\n"
    << source_entry("airport-database-fixture", "Airport Database Fixture", "packages/airport-database.json", "Contains scoped airport database package.") << ",\n"
    << source_entry("runway-surfaces-fixture", "Runway Surfaces Fixture", "packages/runway-surfaces.json", "Contains scoped runway surfaces package.") << ",\n"
    << source_entry("navigation-map-fixture", "Navigation Map Fixture", "packages/navigation-map.json", "Contains scoped offline navigation map package.") << ",\n"
    << source_entry("mask-sources-fixture", "Mask Sources Fixture", "packages/mask-sources.json", "Contains scoped mask source package.") << ",\n"
    << source_entry("world-object-sources-fixture", "World Object Sources Fixture", "packages/world-object-sources.json", "Contains scoped world object source package.") << "\n"
    << "  ]\n"
    << "}\n";
  return output.str();
}

std::string region_manifest() {
  return R"({
  "schemaVersion": "flying.region-manifest.v1",
  "regionId": "pilot-fixture-4m",
  "displayName": "Pilot Fixture Region",
  "coverageScope": "pilot-region",
  "packageMode": "distributable",
  "bounds": {
    "crs": "EPSG:4326",
    "minLonDeg": 16.0,
    "maxLonDeg": 16.1,
    "minLatDeg": 49.0,
    "maxLatDeg": 49.1
  },
  "projectBounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4},
  "sourceScope": {"allowedMarginM": 0.25, "rejectSyntheticFixtures": true},
  "dataRoot": {
    "installVariable": "FLYING_DATA_ROOT",
    "defaultRelativePath": "Flying/Data/Regions/pilot-fixture-4m"
  },
  "runtimeCompatibility": {
    "minAppVersion": "1.0.0-rc",
    "runtimeNetworkRequired": false
  }
}
)";
}

std::string czech_republic_region_manifest() {
  std::string manifest = replace_once(region_manifest(),
                                      "\"regionId\": \"pilot-fixture-4m\"",
                                      "\"regionId\": \"czech-republic-fixture\"");
  manifest = replace_once(manifest,
                          "\"displayName\": \"Pilot Fixture Region\"",
                          "\"displayName\": \"Czech Republic Fixture Region\"");
  manifest = replace_once(manifest,
                          "\"coverageScope\": \"pilot-region\"",
                          "\"coverageScope\": \"czech-republic\"");
  manifest = replace_once(
    manifest,
    "\"projectBounds\": {\"minEastM\": 0, \"maxEastM\": 4, \"minNorthM\": 0, \"maxNorthM\": 4}",
    "\"projectBounds\": {\"minEastM\": 0, \"maxEastM\": 430000, \"minNorthM\": 0, \"maxNorthM\": 280000}");
  manifest = replace_once(manifest,
                          "Flying/Data/Regions/pilot-fixture-4m",
                          "Flying/Data/Regions/czech-republic-fixture");
  return manifest;
}

std::string package_config() {
  return R"({
  "schemaVersion": "flying.pilot-region-package-config.v1",
  "packageIdHint": "pilot-region-fixture",
  "pilotRegion": {
    "id": "pilot-fixture-4m",
    "minEastM": 0,
    "minNorthM": 0,
    "widthM": 4,
    "heightM": 4
  },
  "transform": {
    "sourceToProject": {
      "eastFromSourceXScale": 1,
      "eastFromSourceYScale": 0,
      "eastOffsetM": 0,
      "northFromSourceXScale": 0,
      "northFromSourceYScale": 1,
      "northOffsetM": 0
    }
  },
  "orthoImagery": {
    "tileSizePx": 2,
    "mipLevels": 2,
    "sources": [
      {
        "id": "ortho",
        "path": "ortofoto/pilot.ppm",
        "format": "ppm-p3",
        "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}
      }
    ]
  },
  "vectorLayers": [
    {"category": "roads", "path": "zabaged/roads.geojson", "format": "geojson", "lineWidthM": 1, "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"category": "rail", "path": "zabaged/rail.geojson", "format": "geojson", "lineWidthM": 1, "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"category": "water", "path": "zabaged/water.geojson", "format": "geojson", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"category": "settlements", "path": "zabaged/settlements.geojson", "format": "geojson", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"category": "vegetationAreas", "path": "zabaged/vegetation.geojson", "format": "geojson", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"category": "notableObjects", "path": "zabaged/notable.geojson", "format": "geojson", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"category": "airport", "path": "airports/airport-objects.geojson", "format": "geojson", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"category": "runway", "path": "runways/runway-objects.geojson", "format": "geojson", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}}
  ],
  "geonamesLabels": {
    "path": "geonames/labels.csv",
    "format": "csv",
    "xField": "eastM",
    "yField": "northM",
    "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}
  },
  "packageInputs": [
    {"role": "terrainElevation", "path": "packages/terrain-elevation-manifest.json", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"role": "terrainCollision", "path": "packages/terrain-collision-manifest.json", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"role": "airportDatabase", "path": "packages/airport-database.json", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"role": "runwaySurfaces", "path": "packages/runway-surfaces.json", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"role": "navigationMap", "path": "packages/navigation-map.json", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"role": "maskSources", "path": "packages/mask-sources.json", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}},
    {"role": "worldObjectSources", "path": "packages/world-object-sources.json", "bounds": {"minEastM": 0, "maxEastM": 4, "minNorthM": 0, "maxNorthM": 4}}
  ],
  "masks": {
    "cellSizeM": 1,
    "defaultMaterial": "terrain",
    "materialLayers": [
      {"category": "roads", "material": "asphalt"},
      {"category": "rail", "material": "ballast"},
      {"category": "settlements", "material": "settlement"},
      {"category": "vegetationAreas", "material": "vegetation"},
      {"category": "water", "material": "water"}
    ]
  },
  "worldObjects": {
    "enabled": true,
    "activeCollisionRadiusM": 1500,
    "streamInDistanceM": 8000,
    "streamOutDistanceM": 10000,
    "dmpHeightEstimates": {
      "defaultBuildingHeightM": 9,
      "defaultVegetationHeightM": 16,
      "defaultObstacleHeightM": 32,
      "defaultPowerLineHeightM": 21
    },
    "graphicsProfiles": [
      {"id": "low", "vegetationDensityScale": 0.25, "objectDensityScale": 0.5},
      {"id": "medium", "vegetationDensityScale": 0.65, "objectDensityScale": 0.75},
      {"id": "high", "vegetationDensityScale": 1, "objectDensityScale": 1}
    ]
  }
}
)";
}

std::string czech_republic_package_config() {
  std::string config = replace_once(package_config(),
                                    "\"schemaVersion\": \"flying.pilot-region-package-config.v1\"",
                                    "\"schemaVersion\": \"flying.czech-republic-package-config.v1\",\n  \"coverageScope\": \"czech-republic\"");
  config = replace_once(config, "\"packageIdHint\": \"pilot-region-fixture\"", "\"packageIdHint\": \"czech-republic-fixture\"");
  config = replace_once(config, "\"id\": \"pilot-fixture-4m\"", "\"id\": \"czech-republic-fixture\"");
  config = replace_once(config, "\"widthM\": 4", "\"widthM\": 430000");
  config = replace_once(config, "\"heightM\": 4", "\"heightM\": 280000");
  return config;
}

std::string feature_collection(std::string_view id,
                               std::string_view name,
                               std::string_view geometry_type,
                               std::string_view coordinates) {
  std::ostringstream output;
  output
    << "{\n"
    << "  \"type\": \"FeatureCollection\",\n"
    << "  \"features\": [\n"
    << "    {\n"
    << "      \"type\": \"Feature\",\n"
    << "      \"id\": \"" << id << "\",\n"
    << "      \"properties\": {\"name\": \"" << name << "\"},\n"
    << "      \"geometry\": {\"type\": \"" << geometry_type
    << "\", \"coordinates\": " << coordinates << "}\n"
    << "    }\n"
    << "  ]\n"
    << "}\n";
  return output.str();
}

void write_fixture_inputs(const std::filesystem::path& root) {
  write_file(root / "source-manifest.json", source_manifest());
  write_file(root / "region-manifest.json", region_manifest());
  write_file(root / "pilot-config.json", package_config());
  write_file(root / "ortofoto" / "pilot.ppm",
             "P3\n"
             "4 4\n"
             "255\n"
             "255 0 0  255 32 0  255 64 0  255 96 0\n"
             "0 255 0  32 255 0  64 255 0  96 255 0\n"
             "0 0 255  32 0 255  64 0 255  96 0 255\n"
             "255 255 0  255 255 32  255 255 64  255 255 96\n");
  write_file(root / "zabaged" / "roads.geojson",
             feature_collection("road-1", "Pilot road", "LineString", "[[0,1],[4,1]]"));
  write_file(root / "zabaged" / "rail.geojson",
             feature_collection("rail-1", "Pilot rail", "LineString", "[[0,3],[4,3]]"));
  write_file(root / "zabaged" / "water.geojson",
             feature_collection("water-1",
                                "Pilot pond",
                                "Polygon",
                                "[[[0,0],[2,0],[2,2],[0,2],[0,0]]]"));
  write_file(root / "zabaged" / "settlements.geojson",
             feature_collection("settlement-1",
                                "Pilot settlement",
                                "Polygon",
                                "[[[2,0],[4,0],[4,2],[2,2],[2,0]]]"));
  write_file(root / "zabaged" / "vegetation.geojson",
             feature_collection("vegetation-1",
                                "Pilot forest",
                                "Polygon",
                                "[[[0,2],[2,2],[2,4],[0,4],[0,2]]]"));
  write_file(root / "zabaged" / "notable.geojson",
             "{\n"
             "  \"type\": \"FeatureCollection\",\n"
             "  \"features\": [\n"
             "    {\n"
             "      \"type\": \"Feature\",\n"
             "      \"id\": \"notable-1\",\n"
             "      \"properties\": {\"name\": \"Pilot mast\", \"kind\": \"mast\", \"heightM\": 34},\n"
             "      \"geometry\": {\"type\": \"Point\", \"coordinates\": [3,3]}\n"
             "    },\n"
             "    {\n"
             "      \"type\": \"Feature\",\n"
             "      \"id\": \"power-line-1\",\n"
             "      \"properties\": {\"name\": \"Pilot power line\", \"kind\": \"power_line\"},\n"
             "      \"geometry\": {\"type\": \"LineString\", \"coordinates\": [[1,3.5],[4,3.5]]}\n"
             "    }\n"
             "  ]\n"
             "}\n");
  write_file(root / "airports" / "airport-objects.geojson",
             "{\n"
             "  \"type\": \"FeatureCollection\",\n"
             "  \"features\": [\n"
             "    {\n"
             "      \"type\": \"Feature\",\n"
             "      \"id\": \"windsock-1\",\n"
             "      \"properties\": {\"name\": \"Pilot windsock\", \"kind\": \"windsock\", \"heightM\": 5},\n"
             "      \"geometry\": {\"type\": \"Point\", \"coordinates\": [1.5,2.5]}\n"
             "    }\n"
             "  ]\n"
             "}\n");
  write_file(root / "runways" / "runway-objects.geojson",
             "{\n"
             "  \"type\": \"FeatureCollection\",\n"
             "  \"features\": [\n"
             "    {\n"
             "      \"type\": \"Feature\",\n"
             "      \"id\": \"runway-sign-1\",\n"
             "      \"properties\": {\"name\": \"Pilot runway sign\", \"kind\": \"runway_sign\", \"heightM\": 1.2},\n"
             "      \"geometry\": {\"type\": \"Point\", \"coordinates\": [2.5,2.5]}\n"
             "    }\n"
             "  ]\n"
             "}\n");
  write_file(root / "geonames" / "labels.csv",
             "id,name,eastM,northM,featureClass\n"
             "gn-1,Pilot Village,3,1,P\n"
             "gn-out,Outside,12,12,P\n");
  write_file(root / "packages" / "terrain-elevation-manifest.json", "{}\n");
  write_file(root / "packages" / "terrain-collision-manifest.json", "{}\n");
  write_file(root / "packages" / "airport-database.json", "{}\n");
  write_file(root / "packages" / "runway-surfaces.json", "{}\n");
  write_file(root / "packages" / "navigation-map.json", "{}\n");
  write_file(root / "packages" / "mask-sources.json", "{}\n");
  write_file(root / "packages" / "world-object-sources.json", "{}\n");
}

flying::data_pipeline::PilotRegionPackageOptions make_options(
  const std::filesystem::path& root) {
  flying::data_pipeline::PilotRegionPackageOptions options;
  options.source_manifest_path = root / "source-manifest.json";
  options.region_manifest_path = root / "region-manifest.json";
  options.source_root = root;
  options.package_config_path = root / "pilot-config.json";
  options.output_directory = root / "out";
  options.report_path = root / "reports" / "pilot-region-validation.json";
  options.package_name = "Pilot Region Offline GIS";
  options.package_version = "2026.08.0";
  options.verify_checksums = false;
  return options;
}

} // namespace

int main() {
  namespace pipeline = flying::data_pipeline;

  const std::filesystem::path root = make_temp_root();
  write_fixture_inputs(root);
  pipeline::PilotRegionPackageOptions options = make_options(root);

  const pipeline::PilotRegionPackageResult result =
    pipeline::process_pilot_region_packages(options);
  assert(result.created());
  assert(std::filesystem::exists(root / "out" / "pilot-region-package.json"));
  assert(std::filesystem::exists(root / "out" / "imagery" / "lod0" / "ortho_z0_r0_c0.ppm"));
  assert(std::filesystem::exists(root / "out" / "imagery" / "lod0" / "ortho_z0_r1_c1.ppm"));
  assert(std::filesystem::exists(root / "out" / "imagery" / "lod1" / "ortho_z1_r0_c0.ppm"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "roads.json"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "rail.json"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "water.json"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "settlements.json"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "vegetationAreas.json"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "notableObjects.json"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "airport.json"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "runway.json"));
  assert(std::filesystem::exists(root / "out" / "vectors" / "labels.json"));
  assert(std::filesystem::exists(root / "out" / "masks" / "water-mask.csv"));
  assert(std::filesystem::exists(root / "out" / "masks" / "material-mask.csv"));
  assert(std::filesystem::exists(root / "out" / "world" / "world-objects.json"));

  const std::string package_manifest = read_file(root / "out" / "pilot-region-package.json");
  assert(package_manifest.find("\"schemaVersion\": \"flying.pilot-region-package.v1\"") !=
         std::string::npos);
  assert(package_manifest.find("\"mipmapped\": true") != std::string::npos);
  assert(package_manifest.find("\"level\": 1") != std::string::npos);
  assert(package_manifest.find("\"runtimeNetworkRequired\": false") != std::string::npos);
  assert(package_manifest.find("\"regionManifest\"") != std::string::npos);
  assert(package_manifest.find("\"regionId\": \"pilot-fixture-4m\"") != std::string::npos);
  assert(package_manifest.find("\"packageMode\": \"distributable\"") !=
         std::string::npos);
  assert(package_manifest.find("\"packageInputs\"") != std::string::npos);
  assert(package_manifest.find("\"role\":\"terrainElevation\"") != std::string::npos);
  assert(package_manifest.find("\"role\":\"airportDatabase\"") != std::string::npos);
  assert(package_manifest.find("\"installVariable\": \"FLYING_DATA_ROOT\"") !=
         std::string::npos);
  assert(package_manifest.find("\"streaming\"") != std::string::npos);
  assert(package_manifest.find("\"packagingLayout\"") != std::string::npos);
  assert(package_manifest.find("\"worldObjects\"") != std::string::npos);
  assert(package_manifest.find("\"approvedVectorDataOnly\": true") != std::string::npos);
  assert(package_manifest.find("\"orthoColorInferenceAllowed\": false") != std::string::npos);
  assert(package_manifest.find("\"activeZoneCollisionOnly\": true") != std::string::npos);
  assert(package_manifest.find("\"densityScalesPreserveFlightCriticalObjects\": true") !=
         std::string::npos);
  assert(package_manifest.find("\"criticalObjectTypes\": [\"mast\", \"power_line\", \"obstacle\", \"water_surface\", \"windsock\", \"runway_object\"]") !=
         std::string::npos);
  assert(package_manifest.find("\"waterMaskAvailable\": true") != std::string::npos);
  assert(package_manifest.find("\"materialMaskAvailable\": true") != std::string::npos);
  assert(package_manifest.find("\"externalMapApiKeys\": 0") != std::string::npos);
  assert(package_manifest.find("\"remoteTileServerUrls\": 0") != std::string::npos);
  assert(package_manifest.find("\"CUZK Ortofoto Fixture\"") != std::string::npos);
  assert(package_manifest.find("\"license\"") != std::string::npos);
  assert(package_manifest.find("\"checksum\"") != std::string::npos);
  assert(package_manifest.find("tiles.example") == std::string::npos);
  assert(package_manifest.find("apiKey") == std::string::npos);

  const std::string labels = read_file(root / "out" / "vectors" / "labels.json");
  assert(labels.find("Pilot Village") != std::string::npos);
  assert(labels.find("Outside") == std::string::npos);

  const std::string water_mask = read_file(root / "out" / "masks" / "water-mask.csv");
  assert(water_mask.find(",1\n") != std::string::npos);
  const std::string material_mask = read_file(root / "out" / "masks" / "material-mask.csv");
  assert(material_mask.find(",vegetation\n") != std::string::npos);
  assert(material_mask.find(",water\n") != std::string::npos);

  const std::string world_objects = read_file(root / "out" / "world" / "world-objects.json");
  assert(world_objects.find("\"schemaVersion\": \"flying.world-objects.v1\"") !=
         std::string::npos);
  assert(world_objects.find("\"placementSource\": \"approved_vector_with_dmp_height_estimate\"") !=
         std::string::npos);
  assert(world_objects.find("\"orthoColorInferenceAllowed\": false") != std::string::npos);
  assert(world_objects.find("\"kind\": \"building\"") != std::string::npos);
  assert(world_objects.find("\"heightM\": 9") != std::string::npos);
  assert(world_objects.find("\"kind\": \"vegetation\"") != std::string::npos);
  assert(world_objects.find("\"kind\": \"water_surface\"") != std::string::npos);
  assert(world_objects.find("\"collisionPolicy\": \"water_surface_active_zone\"") !=
         std::string::npos);
  assert(world_objects.find("\"kind\": \"mast\"") != std::string::npos);
  assert(world_objects.find("\"heightM\": 34") != std::string::npos);
  assert(world_objects.find("\"kind\": \"power_line\"") != std::string::npos);
  assert(world_objects.find("\"kind\": \"windsock\"") != std::string::npos);
  assert(world_objects.find("\"audioHook\": \"airport_windsock_wind\"") !=
         std::string::npos);
  assert(world_objects.find("\"kind\": \"runway_object\"") != std::string::npos);
  assert(world_objects.find("\"flightCriticalDensityScale\":1") != std::string::npos);
  assert(world_objects.find("\"hysteresisPreventsHorizonPopping\": true") !=
         std::string::npos);
  assert(world_objects.find("\"environmentAudioHooks\": [\"water_ambience\", \"vegetation_wind\", \"airport_windsock_wind\"]") !=
         std::string::npos);

  const std::string report = read_file(options.report_path);
  assert(report.find("\"passed\": true") != std::string::npos);
  assert(report.find("\"orthoTileCount\": 5") != std::string::npos);
  assert(report.find("\"vectorLayerCount\": 8") != std::string::npos);
  assert(report.find("\"labelCount\": 1") != std::string::npos);

  std::filesystem::remove_all(root);

  {
    const std::filesystem::path remote_root = make_temp_root();
    write_fixture_inputs(remote_root);
    write_file(remote_root / "pilot-config.json",
               replace_once(package_config(),
                            "\"path\": \"ortofoto/pilot.ppm\"",
                            "\"path\": \"https://tiles.example.invalid/{z}/{x}/{y}.png\""));
    pipeline::PilotRegionPackageOptions remote_options = make_options(remote_root);
    const pipeline::PilotRegionPackageResult remote_result =
      pipeline::process_pilot_region_packages(remote_options);
    assert(!remote_result.created());
    assert(!std::filesystem::exists(remote_root / "out" / "pilot-region-package.json"));
    const std::string remote_report = read_file(remote_options.report_path);
    assert(remote_report.find("\"code\": \"pilot.config.orthoImagery.sources.path.remote_url\"") !=
           std::string::npos);
    std::filesystem::remove_all(remote_root);
  }

  {
    const std::filesystem::path czech_root = make_temp_root();
    write_fixture_inputs(czech_root);
    write_file(czech_root / "pilot-config.json", czech_republic_package_config());
    write_file(czech_root / "region-manifest.json", czech_republic_region_manifest());
    pipeline::PilotRegionPackageOptions czech_options = make_options(czech_root);
    czech_options.package_name = "Czech Republic Offline GIS";
    const pipeline::PilotRegionPackageResult czech_result =
      pipeline::process_czech_republic_packages(czech_options);
    assert(!czech_result.created());
    assert(!std::filesystem::exists(czech_root / "out" / "pilot-region-package.json"));
    const std::string czech_report = read_file(czech_options.report_path);
    assert(czech_report.find(
             "\"code\": \"pilot.config.czechRepublic.orthoImagery.coverage_incomplete\"") !=
           std::string::npos);
    std::filesystem::remove_all(czech_root);
  }

  {
    const std::filesystem::path czech_command_root = make_temp_root();
    write_fixture_inputs(czech_command_root);
    write_file(czech_command_root / "region-manifest.json",
               czech_republic_region_manifest());
    pipeline::PilotRegionPackageOptions czech_command_options =
      make_options(czech_command_root);
    czech_command_options.package_name = "Czech Republic Offline GIS";
    const pipeline::PilotRegionPackageResult czech_command_result =
      pipeline::process_czech_republic_packages(czech_command_options);
    assert(!czech_command_result.created());
    const std::string czech_command_report =
      read_file(czech_command_options.report_path);
    assert(czech_command_report.find(
             "\"code\": \"pilot.options.czech_republic_config.required\"") !=
           std::string::npos);
    std::filesystem::remove_all(czech_command_root);
  }

  {
    const std::filesystem::path scope_root = make_temp_root();
    write_fixture_inputs(scope_root);
    write_file(scope_root / "pilot-config.json",
               replace_once(package_config(),
                            "\"bounds\": {\"minEastM\": 0, \"maxEastM\": 4, \"minNorthM\": 0, \"maxNorthM\": 4}",
                            "\"bounds\": {\"minEastM\": -10, \"maxEastM\": 4, \"minNorthM\": 0, \"maxNorthM\": 4}"));
    pipeline::PilotRegionPackageOptions scope_options = make_options(scope_root);
    const pipeline::PilotRegionPackageResult scope_result =
      pipeline::process_pilot_region_packages(scope_options);
    assert(!scope_result.created());
    assert(!std::filesystem::exists(scope_root / "out" / "pilot-region-package.json"));
    const std::string scope_report = read_file(scope_options.report_path);
    assert(scope_report.find(
             "\"code\": \"pilot.sourceScope.orthoImagery.out_of_region\"") !=
           std::string::npos);
    std::filesystem::remove_all(scope_root);
  }

  {
    const std::filesystem::path package_scope_root = make_temp_root();
    write_fixture_inputs(package_scope_root);
    write_file(package_scope_root / "pilot-config.json",
               replace_once(package_config(),
                            "\"role\": \"terrainElevation\", \"path\": \"packages/terrain-elevation-manifest.json\", \"bounds\": {\"minEastM\": 0, \"maxEastM\": 4, \"minNorthM\": 0, \"maxNorthM\": 4}",
                            "\"role\": \"terrainElevation\", \"path\": \"packages/terrain-elevation-manifest.json\", \"bounds\": {\"minEastM\": 0, \"maxEastM\": 14, \"minNorthM\": 0, \"maxNorthM\": 4}"));
    pipeline::PilotRegionPackageOptions package_scope_options =
      make_options(package_scope_root);
    const pipeline::PilotRegionPackageResult package_scope_result =
      pipeline::process_pilot_region_packages(package_scope_options);
    assert(!package_scope_result.created());
    assert(!std::filesystem::exists(package_scope_root / "out" /
                                    "pilot-region-package.json"));
    const std::string package_scope_report =
      read_file(package_scope_options.report_path);
    assert(package_scope_report.find(
             "\"code\": \"pilot.sourceScope.packageInputs.out_of_region\"") !=
           std::string::npos);
    std::filesystem::remove_all(package_scope_root);
  }

  {
    const std::filesystem::path package_missing_root = make_temp_root();
    write_fixture_inputs(package_missing_root);
    write_file(package_missing_root / "pilot-config.json",
               replace_once(package_config(),
                            "    {\"role\": \"terrainElevation\", \"path\": \"packages/terrain-elevation-manifest.json\", \"bounds\": {\"minEastM\": 0, \"maxEastM\": 4, \"minNorthM\": 0, \"maxNorthM\": 4}},\n",
                            ""));
    pipeline::PilotRegionPackageOptions package_missing_options =
      make_options(package_missing_root);
    const pipeline::PilotRegionPackageResult package_missing_result =
      pipeline::process_pilot_region_packages(package_missing_options);
    assert(!package_missing_result.created());
    const std::string package_missing_report =
      read_file(package_missing_options.report_path);
    assert(package_missing_report.find(
             "\"code\": \"pilot.config.packageInputs.role.missing\"") !=
           std::string::npos);
    std::filesystem::remove_all(package_missing_root);
  }

  {
    const std::filesystem::path missing_ortho_root = make_temp_root();
    write_fixture_inputs(missing_ortho_root);
    write_file(missing_ortho_root / "pilot-config.json",
               replace_once(package_config(),
                            "  \"orthoImagery\": {\n",
                            "  \"orthoImageryMissing\": {\n"));
    pipeline::PilotRegionPackageOptions missing_ortho_options =
      make_options(missing_ortho_root);
    const pipeline::PilotRegionPackageResult missing_ortho_result =
      pipeline::process_pilot_region_packages(missing_ortho_options);
    assert(!missing_ortho_result.created());
    const std::string missing_ortho_report =
      read_file(missing_ortho_options.report_path);
    assert(missing_ortho_report.find(
             "\"code\": \"pilot.config.orthoImagery.missing\"") !=
           std::string::npos);
    std::filesystem::remove_all(missing_ortho_root);
  }

  {
    const std::filesystem::path missing_vector_root = make_temp_root();
    write_fixture_inputs(missing_vector_root);
    write_file(missing_vector_root / "pilot-config.json",
               replace_once(package_config(),
                            "\"category\": \"runway\"",
                            "\"category\": \"runwayMissing\""));
    pipeline::PilotRegionPackageOptions missing_vector_options =
      make_options(missing_vector_root);
    const pipeline::PilotRegionPackageResult missing_vector_result =
      pipeline::process_pilot_region_packages(missing_vector_options);
    assert(!missing_vector_result.created());
    const std::string missing_vector_report =
      read_file(missing_vector_options.report_path);
    assert(missing_vector_report.find(
             "\"code\": \"pilot.config.vectorLayers.category.missing\"") !=
           std::string::npos);
    assert(missing_vector_report.find("\"sourceId\": \"runway\"") !=
           std::string::npos);
    std::filesystem::remove_all(missing_vector_root);
  }

  {
    const std::filesystem::path missing_source_metadata_root = make_temp_root();
    write_fixture_inputs(missing_source_metadata_root);
    std::string manifest = source_manifest();
    manifest = replace_once(manifest,
                            "      \"version\": \"2026.08.fixture\",\n",
                            "");
    manifest = replace_once(
      manifest,
      "      \"license\": {\"name\": \"CUZK Open Data / Geonames Fixture License\"},\n",
      "");
    manifest = replace_once(
      manifest,
      "      \"attribution\": {\"text\": \"Contains CUZK Ortofoto pilot fixture imagery.\"},\n",
      "");
    manifest = replace_once(
      manifest,
      "      \"permittedUse\": {\"terrainDerivatives\": true, \"runtimeRedistribution\": true, \"attributionRequired\": true},\n",
      "      \"permittedUse\": {\"terrainDerivatives\": true, \"attributionRequired\": true},\n");
    manifest = replace_once(
      manifest,
      "      \"checksum\": {\"path\": \"ortofoto/pilot.ppm\", \"algorithm\": \"sha256\", \"value\": \"" +
        std::string{kChecksumFixture} + "\"}\n",
      "      \"checksum\": {}\n");
    write_file(missing_source_metadata_root / "source-manifest.json", manifest);
    pipeline::PilotRegionPackageOptions missing_source_metadata_options =
      make_options(missing_source_metadata_root);
    const pipeline::PilotRegionPackageResult missing_source_metadata_result =
      pipeline::process_pilot_region_packages(missing_source_metadata_options);
    assert(!missing_source_metadata_result.created());
    const std::string missing_source_metadata_report =
      read_file(missing_source_metadata_options.report_path);
    assert(missing_source_metadata_report.find("\"code\": \"source.version.missing\"") !=
           std::string::npos);
    assert(missing_source_metadata_report.find("\"code\": \"source.license.missing\"") !=
           std::string::npos);
    assert(missing_source_metadata_report.find("\"code\": \"source.attribution.missing\"") !=
           std::string::npos);
    assert(missing_source_metadata_report.find(
             "\"code\": \"source.permittedUse.runtimeRedistribution.missing\"") !=
           std::string::npos);
    assert(missing_source_metadata_report.find("\"code\": \"source.checksum.path.missing\"") !=
           std::string::npos);
    std::filesystem::remove_all(missing_source_metadata_root);
  }

  {
    const std::filesystem::path mip_root = make_temp_root();
    write_fixture_inputs(mip_root);
    write_file(mip_root / "pilot-config.json",
               replace_once(package_config(), "\"mipLevels\": 2", "\"mipLevels\": 1"));
    pipeline::PilotRegionPackageOptions mip_options = make_options(mip_root);
    const pipeline::PilotRegionPackageResult mip_result =
      pipeline::process_pilot_region_packages(mip_options);
    assert(!mip_result.created());
    assert(!std::filesystem::exists(mip_root / "out" / "pilot-region-package.json"));
    const std::string mip_report = read_file(mip_options.report_path);
    assert(mip_report.find(
             "\"code\": \"pilot.config.orthoImagery.mipLevels.too_low\"") !=
           std::string::npos);
    std::filesystem::remove_all(mip_root);
  }

  {
    const std::filesystem::path traversal_root = make_temp_root();
    write_fixture_inputs(traversal_root);
    write_file(traversal_root / "pilot-config.json",
               replace_once(package_config(),
                            "\"path\": \"zabaged/roads.geojson\"",
                            "\"path\": \"../outside.geojson\""));
    pipeline::PilotRegionPackageOptions traversal_options = make_options(traversal_root);
    const pipeline::PilotRegionPackageResult traversal_result =
      pipeline::process_pilot_region_packages(traversal_options);
    assert(!traversal_result.created());
    assert(!std::filesystem::exists(traversal_root / "out" / "pilot-region-package.json"));
    const std::string traversal_report = read_file(traversal_options.report_path);
    assert(traversal_report.find(
             "\"code\": \"pilot.config.vectorLayers.path.parent_traversal\"") !=
           std::string::npos);
    std::filesystem::remove_all(traversal_root);
  }

  {
    const std::filesystem::path vector_root = make_temp_root();
    write_fixture_inputs(vector_root);
    write_file(vector_root / "zabaged" / "roads.geojson",
               "{\n"
               "  \"type\": \"FeatureCollection\",\n"
               "  \"features\": [\n"
               "    {\n"
               "      \"type\": \"Feature\",\n"
               "      \"id\": \"road-unsafe\",\n"
               "      \"properties\": {\n"
               "        \"name\": \"Pilot road\",\n"
               "        \"apiKey\": \"secret\",\n"
               "        \"urlTemplate\": \"https://tiles.example.invalid/{z}/{x}/{y}.png\",\n"
               "        \"notes\": \"https://tiles.example.invalid/{z}/{x}/{y}.png\",\n"
               "        \"safe\": \"kept\"\n"
               "      },\n"
               "      \"geometry\": {\"type\": \"LineString\", \"coordinates\": [[0,1],[4,1]]}\n"
               "    }\n"
               "  ]\n"
               "}\n");
    pipeline::PilotRegionPackageOptions vector_options = make_options(vector_root);
    const pipeline::PilotRegionPackageResult vector_result =
      pipeline::process_pilot_region_packages(vector_options);
    assert(vector_result.created());
    const std::string roads = read_file(vector_root / "out" / "vectors" / "roads.json");
    assert(roads.find("apiKey") == std::string::npos);
    assert(roads.find("urlTemplate") == std::string::npos);
    assert(roads.find("tiles.example") == std::string::npos);
    assert(roads.find("\"safe\":\"kept\"") != std::string::npos);
    const std::string vector_report = read_file(vector_options.report_path);
    assert(vector_report.find("\"passed\": true") != std::string::npos);
    std::filesystem::remove_all(vector_root);
  }

  {
    const std::filesystem::path disabled_world_root = make_temp_root();
    write_fixture_inputs(disabled_world_root);
    write_file(disabled_world_root / "pilot-config.json",
               replace_once(package_config(), "\"enabled\": true", "\"enabled\": false"));
    pipeline::PilotRegionPackageOptions disabled_world_options =
      make_options(disabled_world_root);
    const pipeline::PilotRegionPackageResult disabled_world_result =
      pipeline::process_pilot_region_packages(disabled_world_options);
    assert(disabled_world_result.created());
    assert(std::filesystem::exists(disabled_world_root / "out" / "world" /
                                   "world-objects.json"));
    const std::string disabled_world_manifest =
      read_file(disabled_world_root / "out" / "pilot-region-package.json");
    assert(disabled_world_manifest.find("\"path\":\"world/world-objects.json\"") !=
           std::string::npos);
    const std::string disabled_world_objects =
      read_file(disabled_world_root / "out" / "world" / "world-objects.json");
    assert(disabled_world_objects.find("\"objects\": [\n  ]") != std::string::npos);
    std::filesystem::remove_all(disabled_world_root);
  }

  {
    const std::filesystem::path density_root = make_temp_root();
    write_fixture_inputs(density_root);
    write_file(density_root / "pilot-config.json",
               replace_once(package_config(),
                            "\"vegetationDensityScale\": 0.25",
                            "\"vegetationDensityScale\": -0.25"));
    pipeline::PilotRegionPackageOptions density_options = make_options(density_root);
    const pipeline::PilotRegionPackageResult density_result =
      pipeline::process_pilot_region_packages(density_options);
    assert(!density_result.created());
    assert(!std::filesystem::exists(density_root / "out" / "pilot-region-package.json"));
    const std::string density_report = read_file(density_options.report_path);
    assert(density_report.find(
             "\"code\": \"pilot.config.worldObjects.graphicsProfiles.densityScale.invalid\"") !=
           std::string::npos);
    std::filesystem::remove_all(density_root);
  }

  {
    const std::filesystem::path water_line_root = make_temp_root();
    write_fixture_inputs(water_line_root);
    write_file(water_line_root / "zabaged" / "water.geojson",
               feature_collection("water-line-1",
                                  "Pilot watercourse",
                                  "LineString",
                                  "[[0.5,0.5],[0.5,3.5]]"));
    pipeline::PilotRegionPackageOptions water_line_options = make_options(water_line_root);
    const pipeline::PilotRegionPackageResult water_line_result =
      pipeline::process_pilot_region_packages(water_line_options);
    assert(water_line_result.created());
    const std::string line_water_mask =
      read_file(water_line_root / "out" / "masks" / "water-mask.csv");
    assert(line_water_mask.find("0,0,0.5,0.5,1\n") != std::string::npos);
    assert(line_water_mask.find("3,0,0.5,3.5,1\n") != std::string::npos);
    std::filesystem::remove_all(water_line_root);
  }

  return 0;
}
