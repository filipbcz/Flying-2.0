#include "flying/core_sim/selected_package.hpp"
#include "package_io.hpp"
#include <set>

namespace flying::core_sim {
namespace {
using namespace package_io;
void require(bool condition, const std::string& message) {
  if (!condition) throw std::invalid_argument(message);
}
JsonValue load(const std::filesystem::path& path) {
  const auto text = read_text_file(path);
  try { return JsonParser(text).parse(); }
  catch (const std::exception& error) { throw std::invalid_argument(path.string() + ": " + error.what()); }
}
std::filesystem::path contained(const std::filesystem::path& root, const std::filesystem::path& relative) {
  require(!relative.empty() && !relative.is_absolute(), "Package path must be relative to the configured data root");
  const auto path = std::filesystem::canonical(root / relative);
  const auto suffix = path.lexically_relative(root);
  require(!suffix.empty() && *suffix.begin() != "..", "Package member escapes configured root: " + relative.string());
  return path;
}
bool inside(const RegionalBounds& bounds, double latitude, double longitude) {
  return std::isfinite(latitude) && std::isfinite(longitude) &&
    latitude >= bounds.min_latitude_deg && latitude <= bounds.max_latitude_deg &&
    longitude >= bounds.min_longitude_deg && longitude <= bounds.max_longitude_deg;
}
SelectedPackageConfiguration resolve(const std::filesystem::path& root,
                                      const std::filesystem::path& relative,
                                      std::string_view expected) {
  SelectedPackageConfiguration config;
  config.data_root = std::filesystem::canonical(root);
  config.package_location = contained(config.data_root, relative);
  const auto manifest_path = config.package_location / "install-manifest.json";
  config.manifest_sha256 = sha256_file(manifest_path);
  require(expected.size() == 64 && config.manifest_sha256 == expected,
          "Installed package manifest SHA-256 mismatch; repair or reinstall the selected package");
  const auto manifest_value = load(manifest_path);
  const auto& manifest = as_object(manifest_value, "install-manifest");
  config.package_id = require_string(manifest, "regionId", "install-manifest");
  config.version = require_string(manifest, "version", "install-manifest");
  require(!config.package_id.empty() && !config.version.empty(), "Package identity and version are required");
  require(!require_bool(manifest, "runtimeNetworkRequired", "install-manifest"), "Selected package is not offline compatible");
  const auto& files = require_object(manifest, "files", "install-manifest");
  for (const auto& [name, checksum] : files) {
    require(checksum.type == JsonValue::Type::string_value && checksum.string.size() == 64,
            "Invalid package checksum: " + name);
    require(sha256_file(contained(config.package_location, name)) == checksum.string,
            "Corrupt installed package member: " + name + "; repair the selected package");
  }
  for (const auto* required : {"region.json", "airports.json", "safe-starts.json", "runway-surfaces.json"})
    require(files.contains(required), std::string("Required installed data missing from manifest: ") + required);
  require(std::any_of(files.begin(), files.end(), [](const auto& file) {
    return file.first.starts_with("generated/terrain/");
  }), "Installed package has no terrain data");
  const auto region_value = load(config.package_location / "region.json");
  const auto& region = as_object(region_value, "region");
  require(require_string(region, "schemaVersion", "region") == "flying.region-manifest.v1", "Incompatible region schema");
  require(require_string(region, "regionId", "region") == config.package_id, "Region identity differs from install manifest");
  const auto& compatibility = require_object(region, "runtimeCompatibility", "region");
  require(require_string(compatibility, "minAppVersion", "runtimeCompatibility") == "1.0.0-rc" &&
          !require_bool(compatibility, "runtimeNetworkRequired", "runtimeCompatibility"), "Incompatible regional runtime version or offline requirement");
  const auto& bounds = require_object(region, "bounds", "region");
  config.crs = require_string(bounds, "crs", "bounds");
  require(config.crs == "EPSG:4326", "Unsupported regional geographic CRS; expected EPSG:4326");
  config.bounds = {require_number(bounds, "minLatDeg", "bounds"), require_number(bounds, "maxLatDeg", "bounds"),
                   require_number(bounds, "minLonDeg", "bounds"), require_number(bounds, "maxLonDeg", "bounds")};
  require(config.bounds.min_latitude_deg >= -90 && config.bounds.max_latitude_deg <= 90 &&
          config.bounds.min_longitude_deg >= -180 && config.bounds.max_longitude_deg <= 180 &&
          config.bounds.min_latitude_deg < config.bounds.max_latitude_deg &&
          config.bounds.min_longitude_deg < config.bounds.max_longitude_deg, "Invalid regional bounds");
  const auto airport_value = load(config.package_location / "airports.json");
  const auto& database = as_object(airport_value, "airports");
  require(require_string(database, "schemaVersion", "airports") == "flying.aerodrome-database.v1", "Incompatible airport schema");
  const auto starts_value = load(config.package_location / "safe-starts.json");
  const auto& starts = require_array(as_object(starts_value, "safe-starts"), "starts", "safe-starts");
  const auto& airports = require_array(database, "aerodromes", "airports");
  require(!airports.empty(), "Package contains no airport metadata for the georeferenced origin");
  const auto& origin = require_object(as_object(airports.front(), "airport"), "referencePointWgs84", "airport");
  config.origin_latitude_deg = require_number(origin, "latDeg", "origin");
  config.origin_longitude_deg = require_number(origin, "lonDeg", "origin");
  config.origin_height_m = require_number(origin, "elevationM", "origin");
  require(inside(config.bounds, config.origin_latitude_deg, config.origin_longitude_deg), "Package origin is outside regional bounds");
  const auto surfaces_value = load(config.package_location / "runway-surfaces.json");
  const auto& surfaces = require_array(as_object(surfaces_value, "runway-surfaces"), "surfaces", "runway-surfaces");
  std::set<std::string> identities;
  for (const auto& value : starts) {
    const auto& start = as_object(value, "safe-start");
    PilotScenarioLocation location;
    location.location_id = require_string(start, "id", "safe-start");
    require(!location.location_id.empty() && identities.insert(location.location_id).second, "Empty or duplicate safe-start identity");
    location.aerodrome_id = require_string(start, "airportId", "safe-start");
    location.runway_id = require_string(start, "runwayId", "safe-start");
    const auto& position = require_object(start, "positionWgs84", "safe-start");
    location.latitude_deg = require_number(position, "latDeg", "positionWgs84");
    location.longitude_deg = require_number(position, "lonDeg", "positionWgs84");
    location.elevation_m = require_number(position, "elevationM", "positionWgs84");
    location.true_heading_deg = require_number(start, "trueHeadingDeg", "safe-start");
    require(inside(config.bounds, location.latitude_deg, location.longitude_deg), "Safe start is outside regional terrain bounds: " + location.location_id);
    location.ground_eligible = false;
    location.diagnostic = "Ground start is closed, unverified, missing provenance or not an eligible generated safe start";
    bool runway_found = false;
    for (const auto& airport_value_entry : airports) {
      const auto& airport = as_object(airport_value_entry, "airport");
      if (require_string(airport, "id", "airport") != location.aerodrome_id) continue;
      location.display_name = require_string(airport, "name", "airport") + " / " + location.runway_id + " / " + location.location_id;
      for (const auto& runway_value : require_array(airport, "runways", "airport")) {
        const auto& runway = as_object(runway_value, "runway");
        if (require_string(runway, "id", "runway") != location.runway_id) continue;
        runway_found = true;
        const auto& validation = require_object(runway, "validation", "runway");
        const auto& provenance = require_array(runway, "provenance", "runway");
        bool complete_provenance = !provenance.empty();
        for (const auto& reference_value : provenance) {
          const auto& reference = as_object(reference_value, "provenance");
          const auto source = require_string(reference, "sourceDocument", "provenance");
          const auto& checksum = require_object(reference, "checksum", "provenance");
          complete_provenance = complete_provenance && files.contains(source) &&
            require_string(checksum, "algorithm", "checksum") == "sha256" &&
            require_string(checksum, "value", "checksum") == files.at(source).string;
        }
        bool surface_available = false;
        for (const auto& surface_value : surfaces) {
          const auto& surface = as_object(surface_value, "surface");
          if (require_string(surface, "airportId", "surface") == location.aerodrome_id &&
              require_string(surface, "runwayId", "surface") == location.runway_id)
            surface_available = files.contains(require_string(surface, "file", "surface"));
        }
        location.ground_eligible = require_bool(start, "defaultEligible", "safe-start") &&
          !require_bool(start, "requiresUnverifiedFieldOptIn", "safe-start") &&
          require_string(airport, "operationalStatus", "airport") == "active" &&
          require_string(airport, "verificationStatus", "airport") == "verified" &&
          require_string(validation, "status", "runway.validation") == "production_validated" &&
          require_string(require_object(validation, "manualVerification", "runway.validation"), "status", "manualVerification") == "verified" &&
          complete_provenance && surface_available;
      }
    }
    require(!location.display_name.empty() && runway_found, "Safe start references an unknown airport or runway: " + location.aerodrome_id + "/" + location.runway_id);
    if (location.ground_eligible) location.diagnostic.clear();
    config.locations.push_back(std::move(location));
  }
  return config;
}
}

void SelectedPackageRuntime::clear() noexcept { selected_.reset(); ++generation_; }
void SelectedPackageRuntime::select(const std::filesystem::path& root, const std::filesystem::path& relative, std::string_view expected) {
  clear();
  auto config = resolve(root, relative, expected);
  config.generation = generation_;
  selected_ = std::move(config);
}
const SelectedPackageConfiguration& SelectedPackageRuntime::current() const {
  if (!selected_) throw std::invalid_argument("No validated regional package selected; configure the installed data root, package path and trusted manifest hash");
  return *selected_;
}
std::uint64_t SelectedPackageRuntime::generation() const noexcept { return generation_; }
void SelectedPackageRuntime::revalidate() const {
  const auto config = current();
  (void)resolve(config.data_root, config.package_location.lexically_relative(config.data_root), config.manifest_sha256);
}
ScenarioSelection SelectedPackageRuntime::selection(std::string id, ScenarioStartMode mode) const {
  return {std::move(id), mode, current().generation};
}
ScenarioInitialState SelectedPackageRuntime::start(const ScenarioSelection& request, bool aircraft_available) const {
  require(request.package_generation == current().generation, "Stale package selection; refresh locations and select the start again");
  revalidate();
  require(aircraft_available, "Selected aircraft is not installed or validated");
  require(request.start_mode == ScenarioStartMode::ColdAndDark || request.start_mode == ScenarioStartMode::ReadyToTaxi || request.start_mode == ScenarioStartMode::Airborne, "Unsupported start mode");
  const auto& locations = current().locations;
  const auto found = std::find_if(locations.begin(), locations.end(), [&](const auto& location) { return location.location_id == request.location_id; });
  require(found != locations.end(), "Start identity is absent from the selected package");
  if (request.start_mode == ScenarioStartMode::Airborne)
    return airborne(request, *found, found->elevation_m + 450.0, aircraft_available);
  require(found->ground_eligible, found->diagnostic + ": " + request.location_id);
  return make_scenario_initial_state(request, locations);
}
ScenarioInitialState SelectedPackageRuntime::airborne(const ScenarioSelection& request, PilotScenarioLocation position, double altitude, bool aircraft_available) const {
  require(request.package_generation == current().generation, "Stale package selection; refresh the scenario editor");
  revalidate();
  require(aircraft_available, "Selected aircraft is not installed or validated");
  require(request.start_mode == ScenarioStartMode::Airborne, "Geographic ground starts are forbidden; choose an eligible generated safe start");
  require(inside(current().bounds, position.latitude_deg, position.longitude_deg), "Airborne start is outside selected regional terrain bounds");
  require(static_cast<bool>(terrain_elevation_), "Terrain sampling is unavailable; install a compatible terrain provider before starting airborne");
  const auto elevation = terrain_elevation_(current(), position.latitude_deg, position.longitude_deg);
  require(elevation.has_value() && std::isfinite(*elevation), "Required terrain data is unavailable at the airborne position");
  require(std::isfinite(altitude) && altitude > *elevation + 1.5, "Airborne altitude must be above the installed terrain");
  position.elevation_m = altitude - 450.0;
  position.location_id = request.location_id;
  position.selectable = true;
  auto state = make_scenario_initial_state(request, std::span<const PilotScenarioLocation>(&position, 1));
  state.flight_dynamics_initial_condition.terrain_elevation_m = *elevation;
  return state;
}
void SelectedPackageRuntime::set_terrain_elevation_provider(TerrainElevationProvider provider) { terrain_elevation_ = std::move(provider); }
SelectedPackageRuntime& selected_package_runtime() { static SelectedPackageRuntime runtime; return runtime; }
}
