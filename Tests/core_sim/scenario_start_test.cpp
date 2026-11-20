#include "flying/core_sim/selected_package.hpp"
#include "../../core_sim/src/package_io.hpp"
#include <chrono>
#include <iostream>
#include <fstream>
#include <limits>
#include <stdexcept>

using namespace flying::core_sim;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Action> void rejects(Action action, std::string_view diagnostic) {
  try { action(); } catch (const std::exception& error) {
    check(std::string(error.what()).find(diagnostic) != std::string::npos, error.what()); return;
  }
  throw std::runtime_error("Expected start or package rejection");
}
void write(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary); output << text;
  check(static_cast<bool>(output), "Cannot write test package");
}
std::string fixture(const std::filesystem::path& root, const std::string& status = "verified", const std::string& operation = "active", const std::string& minimum_version = "1.0.0-rc") {
  write(root / "region.json", R"({"schemaVersion":"flying.region-manifest.v1","regionId":"test-region","bounds":{"crs":"EPSG:4326","minLatDeg":49,"maxLatDeg":50,"minLonDeg":16,"maxLonDeg":17},"runtimeCompatibility":{"minAppVersion":")" + minimum_version + R"(","runtimeNetworkRequired":false}})");
  write(root / "airports.json", R"({"schemaVersion":"flying.aerodrome-database.v1","aerodromes":[{"id":"TEST-AIRPORT","name":"Package airport","operationalStatus":")" + operation + R"(","verificationStatus":")" + status + R"(","referencePointWgs84":{"latDeg":49.5,"lonDeg":16.5,"elevationM":400},"runways":[{"id":"runway-alpha","validation":{"status":"production_validated","manualVerification":{"status":"verified"}},"provenance":[{"sourceDocument":"source.txt","checksum":{"algorithm":"sha256","value":")" + package_io::sha256_text("approved source") + R"("}}]}]}]})");
  write(root / "source.txt", "approved source");
  write(root / "safe-starts.json", R"({"starts":[{"id":"generated-safe-alpha","airportId":"TEST-AIRPORT","runwayId":"runway-alpha","positionWgs84":{"latDeg":49.5,"lonDeg":16.5,"elevationM":400},"trueHeadingDeg":150,"defaultEligible":true,"requiresUnverifiedFieldOptIn":false}]})");
  write(root / "runway-surfaces.json", R"({"schemaVersion":"flying.pilot-runway-surfaces-package.v1","surfaces":[{"airportId":"TEST-AIRPORT","runwayId":"runway-alpha","file":"surfaces/alpha.json"}]})");
  write(root / "surfaces/alpha.json", "{}");
  write(root / "generated/terrain/test.npz", "terrain contract test payload");
  std::string manifest = R"({"regionId":"test-region","version":"test-version","runtimeNetworkRequired":false,"files":{)";
  bool first = true;
  for (const auto* name : {"region.json", "airports.json", "safe-starts.json", "runway-surfaces.json", "surfaces/alpha.json", "generated/terrain/test.npz", "source.txt"}) {
    if (!first) manifest += ",";
    first = false;
    manifest += "\"" + std::string(name) + "\":\"" + package_io::sha256_file(root / name) + "\"";
  }
  manifest += "}}";
  write(root / "install-manifest.json", manifest);
  return package_io::sha256_file(root / "install-manifest.json");
}
void regression(const std::filesystem::path& root) {
  auto& runtime = selected_package_runtime();
  rejects([&] { (void)runtime.current(); }, "No validated");
  const auto hash = fixture(root / "installed/alpha");
  runtime.select(root / "installed", "alpha", hash);
  const auto config = runtime.current();
  check(config.package_id == "test-region" && config.version == "test-version" && config.crs == "EPSG:4326", "Metadata identity must be published");
  check(config.origin_latitude_deg == 49.5 && config.origin_height_m == 400, "Origin must derive from metadata");
  check(config.locations.size() == 1 && config.locations.front().runway_id == "runway-alpha", "Catalog must derive from generated identities");
  const auto catalog_airborne = runtime.selection("generated-safe-alpha", ScenarioStartMode::Airborne);
  rejects([&] { (void)runtime.start(catalog_airborne, true); }, "Terrain sampling");
  rejects([&] { CoreSimulator simulator; (void)reset_simulator_to_scenario(simulator, catalog_airborne); }, "Terrain sampling");
  runtime.set_terrain_elevation_provider([&](const auto& selected, double latitude, double longitude) -> std::optional<double> {
    check(selected.data_root == runtime.current().data_root && selected.package_location == runtime.current().package_location &&
          selected.generation == runtime.generation(), "Terrain sampling must use the current relocated package configuration");
    check(latitude == 49.5 && longitude == 16.5, "Catalog terrain query must use the selected start position");
    return 425;
  });
  for (auto mode : {ScenarioStartMode::ColdAndDark, ScenarioStartMode::ReadyToTaxi, ScenarioStartMode::Airborne}) {
    const auto request = runtime.selection("generated-safe-alpha", mode);
    const auto state = runtime.start(request, true);
    check(state.engine_running == (mode != ScenarioStartMode::ColdAndDark), "Start mode must preserve engine state");
    check(state.parking_brake_set == (mode == ScenarioStartMode::ColdAndDark), "Ground mode controls must remain distinct");
    if (mode == ScenarioStartMode::Airborne) {
      check(state.flight_dynamics_initial_condition.altitude_m == 850, "Catalog airborne altitude must preserve the generated start offset");
      check(state.flight_dynamics_initial_condition.terrain_elevation_m == 425, "Catalog airborne terrain must use the provider, not safe-start elevation");
    }
    CoreSimulator simulator;
    check(reset_simulator_to_scenario(simulator, request).selection.package_generation == config.generation, "Reset must retain selected package generation");
  }
  const auto stale = runtime.selection("generated-safe-alpha", ScenarioStartMode::ReadyToTaxi);
  std::filesystem::rename(root / "installed", root / "relocated");
  runtime.select(root / "relocated", "alpha", hash);
  check(runtime.current().data_root == std::filesystem::canonical(root / "relocated"), "Data root relocation must not require code edits");
  rejects([&] { (void)runtime.start(stale, true); }, "Stale");
  const auto request = runtime.selection("generated-safe-alpha", ScenarioStartMode::Airborne);
  check(runtime.start(request, true).flight_dynamics_initial_condition.terrain_elevation_m == 425, "Relocated catalog start must query current terrain");
  rejects([&] { (void)runtime.start(request, false); }, "aircraft");
  rejects([&] { (void)runtime.start(runtime.selection("unknown", ScenarioStartMode::ReadyToTaxi), true); }, "absent");
  PilotScenarioLocation point; point.latitude_deg = 49.5; point.longitude_deg = 16.5; point.true_heading_deg = 20;
  runtime.set_terrain_elevation_provider({});
  rejects([&] { (void)runtime.start(request, true); }, "Terrain sampling");
  rejects([&] { (void)runtime.airborne(request, point, 850, true); }, "Terrain sampling");
  runtime.set_terrain_elevation_provider([](const auto& config_value, double, double) -> std::optional<double> {
    check(config_value.crs == "EPSG:4326", "Terrain provider must receive selected configuration"); return 400;
  });
  check(runtime.airborne(request, point, 850, true).flight_dynamics_initial_condition.altitude_m == 850, "Airborne altitude must survive terrain validation");
  rejects([&] { (void)runtime.airborne(request, point, 399, true); }, "above");
  point.latitude_deg = 51;
  rejects([&] { (void)runtime.airborne(request, point, 850, true); }, "outside");
  point.latitude_deg = 49.5;
  rejects([&] { (void)runtime.airborne(runtime.selection("custom", ScenarioStartMode::ReadyToTaxi), point, 850, true); }, "ground starts");
  runtime.set_terrain_elevation_provider([](const auto&, double, double) -> std::optional<double> { return std::nullopt; });
  rejects([&] { (void)runtime.start(request, true); }, "terrain data");
  rejects([&] { (void)runtime.airborne(request, point, 850, true); }, "terrain data");
  check(runtime.start(runtime.selection("generated-safe-alpha", ScenarioStartMode::ReadyToTaxi), true).engine_running, "Missing airborne terrain must not change eligible ground starts");
  for (const auto elevation : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
    runtime.set_terrain_elevation_provider([=](const auto&, double, double) -> std::optional<double> { return elevation; });
    rejects([&] { (void)runtime.start(request, true); }, "terrain data");
    rejects([&] { (void)runtime.airborne(request, point, 850, true); }, "terrain data");
  }
  for (const auto elevation : {848.5, 850.0, 900.0}) {
    runtime.set_terrain_elevation_provider([=](const auto&, double, double) -> std::optional<double> { return elevation; });
    rejects([&] { (void)runtime.start(request, true); }, "above the installed terrain");
    rejects([&] { (void)runtime.airborne(request, point, 850, true); }, "above the installed terrain");
  }
  write(root / "relocated/alpha/airports.json", "corrupt");
  rejects([&] { (void)runtime.start(request, true); }, "Corrupt");
  rejects([&] { runtime.select(root / "relocated", "alpha", hash); }, "Corrupt");
  rejects([&] { (void)runtime.current(); }, "No validated");
  const auto unverified = fixture(root / "unverified", "unverified");
  runtime.select(root, "unverified", unverified);
  rejects([&] { (void)runtime.start(runtime.selection("generated-safe-alpha", ScenarioStartMode::ReadyToTaxi), true); }, "unverified");
  runtime.set_terrain_elevation_provider([](const auto&, double, double) -> std::optional<double> { return 400; });
  check(runtime.start(runtime.selection("generated-safe-alpha", ScenarioStartMode::Airborne), true).engine_running, "Unverified ground data must not prevent terrain-backed airborne mode");
  const auto closed = fixture(root / "closed", "verified", "closed");
  runtime.select(root, "closed", closed);
  rejects([&] { (void)runtime.start(runtime.selection("generated-safe-alpha", ScenarioStartMode::ColdAndDark), true); }, "closed");
  const auto incompatible = fixture(root / "incompatible", "verified", "active", "999.0");
  rejects([&] { runtime.select(root, "incompatible", incompatible); }, "Incompatible");
  rejects([&] { runtime.select(root, "missing", hash); }, "");
  rejects([&] { runtime.select(root, "closed", std::string(64, '0')); }, "SHA-256");
  std::filesystem::remove(root / "closed/safe-starts.json");
  rejects([&] { runtime.select(root, "closed", closed); }, "");
  runtime.clear();
  runtime.set_terrain_elevation_provider({});
}
}
int main(int argc, char** argv) {
  try {
    if (argc == 5 && std::string(argv[1]) == "--production") {
      auto& runtime = selected_package_runtime(); runtime.select(argv[2], argv[3], argv[4]);
      check(!runtime.current().locations.empty(), "Production package must expose locations");
      for (const auto& location : runtime.current().locations) {
        std::cout << location.location_id << " | " << location.aerodrome_id << " | " << location.runway_id << " | " << location.diagnostic << '\n';
        if (!location.ground_eligible) rejects([&] { (void)runtime.start(runtime.selection(location.location_id, ScenarioStartMode::ColdAndDark), true); }, "unverified");
        rejects([&] { (void)runtime.start(runtime.selection(location.location_id, ScenarioStartMode::Airborne), true); }, "Terrain sampling");
      }
      std::cout << "Production installed package validated: " << runtime.current().package_id << " " << runtime.current().version << '\n';
      return 0;
    }
    const auto root = std::filesystem::temp_directory_path() / ("flying-selected-package-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try { regression(root); } catch (...) { std::filesystem::remove_all(root); throw; }
    std::filesystem::remove_all(root);
    std::cout << "Package-driven scenario and shared configuration regressions passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
