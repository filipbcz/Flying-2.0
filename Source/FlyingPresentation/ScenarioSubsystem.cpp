#include "ScenarioSubsystem.h"

#include "flying/core_sim/aircraft_config.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <exception>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace flying::presentation {
namespace {

[[nodiscard]] bool is_finite(double value) noexcept {
  return std::isfinite(value);
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return {};
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool extract_json_string_field(const std::string& text,
                                             std::string_view field,
                                             std::string& value) {
  const std::string key = "\"" + std::string(field) + "\"";
  const std::size_t key_pos = text.find(key);
  if (key_pos == std::string::npos) {
    return false;
  }

  std::size_t pos = key_pos + key.size();
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  if (pos >= text.size() || text[pos] != ':') {
    return false;
  }
  ++pos;
  while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  if (pos >= text.size() || text[pos] != '"') {
    return false;
  }
  ++pos;

  std::string parsed;
  while (pos < text.size()) {
    const char character = text[pos++];
    if (character == '"') {
      value = parsed;
      return true;
    }
    if (character == '\\') {
      if (pos >= text.size()) {
        return false;
      }
      parsed.push_back(text[pos++]);
    } else {
      parsed.push_back(character);
    }
  }
  return false;
}

void require_manifest(const std::filesystem::path& path,
                      const std::string& label,
                      const std::string& schema_version,
                      ScenarioAvailabilityReport& report) {
  if (path.empty()) {
    report.errors.push_back(label + " manifest path is empty");
    return;
  }
  if (!std::filesystem::exists(path)) {
    report.errors.push_back(label + " manifest is unavailable: " + path.string());
    return;
  }

  const std::string text = read_file(path);
  if (text.empty()) {
    report.errors.push_back(label + " manifest is unreadable or empty: " + path.string());
    return;
  }
  std::string actual_schema_version;
  if (!extract_json_string_field(text, "schemaVersion", actual_schema_version) ||
      actual_schema_version != schema_version) {
    report.errors.push_back(label + " manifest has unsupported schema: " + path.string());
  }
}

[[nodiscard]] flying::core_sim::AircraftConfigurationLoadResult load_aircraft_config(
    const std::filesystem::path& path,
    ScenarioAvailabilityReport& report) {
  if (path.empty()) {
    report.errors.push_back("aircraft configuration path is empty");
    return {};
  }
  if (!std::filesystem::exists(path)) {
    report.errors.push_back("aircraft configuration is unavailable: " + path.string());
    return {};
  }

  auto loaded = flying::core_sim::load_aircraft_configuration(path);
  if (!loaded.loaded) {
    if (loaded.errors.empty()) {
      report.errors.push_back("aircraft configuration could not be loaded: " + path.string());
    } else {
      report.errors.insert(report.errors.end(), loaded.errors.begin(), loaded.errors.end());
    }
    return loaded;
  }
  if (loaded.configuration.schema_version != flying::core_sim::kAircraftConfigSchemaVersion) {
    report.errors.push_back("aircraft configuration has unsupported schema: " + path.string());
  }
  return loaded;
}

void require_aircraft_config(const std::filesystem::path& path,
                             ScenarioAvailabilityReport& report) {
  static_cast<void>(load_aircraft_config(path, report));
}

void validate_setup(const ScenarioSetup& setup, ScenarioStartResult& result) {
  if (setup.aircraft_id.empty()) {
    result.errors.push_back("aircraft id is required");
  }
  if (!is_finite(setup.pilot_and_payload_weight_kg) || setup.pilot_and_payload_weight_kg < 0.0) {
    result.errors.push_back("pilot and payload weight must be finite and non-negative");
  }
  if (!is_finite(setup.fuel_weight_kg) || setup.fuel_weight_kg < 0.0) {
    result.errors.push_back("fuel weight must be finite and non-negative");
  }
  if (!is_finite(setup.local_time_seconds) ||
      setup.local_time_seconds < 0.0 ||
      setup.local_time_seconds >= 86400.0) {
    result.errors.push_back("local time must be within one day");
  }
  if (setup.local_year < 1970 ||
      setup.local_month < 1 ||
      setup.local_month > 12 ||
      setup.local_day < 1 ||
      setup.local_day > 31) {
    result.errors.push_back("local date must be a valid calendar selection range");
  }
  if (setup.position_mode == ScenarioPositionMode::AirportOrRunway && setup.location_id.empty()) {
    result.errors.push_back("airport or runway scenario location id is required");
  }
  if (setup.position_mode == ScenarioPositionMode::GeographicPosition) {
    if (!is_finite(setup.latitude_deg) ||
        setup.latitude_deg < -90.0 ||
        setup.latitude_deg > 90.0) {
      result.errors.push_back("geographic latitude must be finite and in [-90, 90]");
    }
    if (!is_finite(setup.longitude_deg) ||
        setup.longitude_deg < -180.0 ||
        setup.longitude_deg > 180.0) {
      result.errors.push_back("geographic longitude must be finite and in [-180, 180]");
    }
    if (!is_finite(setup.altitude_m)) {
      result.errors.push_back("geographic altitude must be finite");
    }
    if (!is_finite(setup.true_heading_deg) ||
        setup.true_heading_deg < 0.0 ||
        setup.true_heading_deg >= 360.0) {
      result.errors.push_back("geographic true heading must be finite and in [0, 360)");
    }
  }
}

[[nodiscard]] flying::core_sim::AircraftLoadout make_total_loadout(
    const flying::core_sim::AircraftConfiguration& configuration,
    double fuel_weight_kg,
    double payload_weight_kg) {
  flying::core_sim::AircraftLoadout loadout{};

  double remaining_fuel_kg = fuel_weight_kg;
  for (const auto& station : configuration.mass_balance.fuel_stations) {
    const double assigned = std::min(remaining_fuel_kg, station.capacity_kg.value);
    loadout.fuel.push_back({station.id, assigned});
    remaining_fuel_kg -= assigned;
  }

  double remaining_payload_kg = payload_weight_kg;
  for (const auto& station : configuration.mass_balance.payload_stations) {
    const double assigned = std::min(remaining_payload_kg, station.max_mass_kg.value);
    loadout.payload.push_back({station.id, assigned});
    remaining_payload_kg -= assigned;
  }

  if (remaining_fuel_kg > 1.0e-9) {
    loadout.fuel.push_back({"unavailable-fuel-capacity", remaining_fuel_kg});
  }
  if (remaining_payload_kg > 1.0e-9) {
    loadout.payload.push_back({"unavailable-payload-capacity", remaining_payload_kg});
  }
  return loadout;
}

[[nodiscard]] bool apply_failure_selection(flying::core_sim::FailureStateModel& failures,
                                           const ScenarioFailureSelection& selection) {
  if (selection.failure_id == "pitot_blocked") {
    failures.pitot_blocked = selection.failed;
  } else if (selection.failure_id == "static_port_blocked") {
    failures.static_port_blocked = selection.failed;
  } else if (selection.failure_id == "gps_failed") {
    failures.gps_failed = selection.failed;
  } else if (selection.failure_id == "alternator_failed") {
    failures.alternator_failed = selection.failed;
  } else if (selection.failure_id == "battery_failed") {
    failures.battery_failed = selection.failed;
  } else if (selection.failure_id == "avionics_bus_failed") {
    failures.avionics_bus_failed = selection.failed;
  } else if (selection.failure_id == "vacuum_pump_failed") {
    failures.vacuum_pump_failed = selection.failed;
  } else if (selection.failure_id == "standby_vacuum_pump_failed") {
    failures.standby_vacuum_pump_failed = selection.failed;
  } else if (selection.failure_id == "fuel_left_tank_blocked") {
    failures.fuel_left_tank_blocked = selection.failed;
  } else if (selection.failure_id == "fuel_right_tank_blocked") {
    failures.fuel_right_tank_blocked = selection.failed;
  } else if (selection.failure_id == "engine_driven_fuel_pump_failed") {
    failures.engine_driven_fuel_pump_failed = selection.failed;
  } else if (selection.failure_id == "electric_fuel_pump_failed") {
    failures.electric_fuel_pump_failed = selection.failed;
  } else if (selection.failure_id == "pitot_heat_failed") {
    failures.pitot_heat_failed = selection.failed;
  } else if (selection.failure_id == "engine_sensor_power_failed") {
    failures.engine_sensor_power_failed = selection.failed;
  } else {
    return false;
  }
  return true;
}

void apply_runtime_setup(const ScenarioSetup& setup,
                         const flying::core_sim::AircraftConfiguration& aircraft,
                         ScenarioStartResult& result) {
  result.runtime.aircraft_id = setup.aircraft_id;
  result.runtime.local_year = setup.local_year;
  result.runtime.local_month = setup.local_month;
  result.runtime.local_day = setup.local_day;
  result.runtime.local_time_seconds = setup.local_time_seconds;
  result.runtime.weather = setup.weather;

  const flying::core_sim::AircraftLoadout loadout =
      make_total_loadout(aircraft,
                         setup.fuel_weight_kg,
                         setup.pilot_and_payload_weight_kg);
  result.runtime.mass_balance =
      flying::core_sim::compute_aircraft_mass_balance(aircraft, loadout);
  result.initial_state.rigid_body_state.aircraft_mass_balance = result.runtime.mass_balance;
  result.initial_state.rigid_body_state.weather =
      flying::core_sim::sample_weather(setup.weather,
                                       result.initial_state.location.latitude_deg,
                                       result.initial_state.location.longitude_deg,
                                       result.initial_state.flight_dynamics_initial_condition.altitude_m,
                                       setup.local_time_seconds);
}

void apply_runtime_failures(const ScenarioSetup& setup, ScenarioStartResult& result) {
  for (const ScenarioFailureSelection& failure : setup.failures) {
    if (!apply_failure_selection(result.runtime.failures, failure)) {
      result.errors.push_back("unsupported failure selection: " + failure.failure_id);
    }
  }
}

} // namespace

ScenarioAvailabilityReport check_scenario_data_availability(
    const ScenarioAvailabilityInput& input) {
  ScenarioAvailabilityReport report{};
  require_manifest(input.terrain_manifest_path,
                   "terrain",
                   "flying.terrain-slice.v1",
                   report);
  require_manifest(input.visual_manifest_path,
                   "visual",
                   "flying.visual-package-manifest.v1",
                   report);
  require_aircraft_config(input.aircraft_config_path, report);
  report.available = report.errors.empty();
  return report;
}

core_sim::ScenarioStartMode to_core_start_mode(ScenarioStartMode mode) noexcept {
  switch (mode) {
  case ScenarioStartMode::ColdAndDark:
    return core_sim::ScenarioStartMode::ColdAndDark;
  case ScenarioStartMode::ReadyToTaxi:
    return core_sim::ScenarioStartMode::ReadyToTaxi;
  case ScenarioStartMode::Airborne:
    return core_sim::ScenarioStartMode::Airborne;
  }
  return core_sim::ScenarioStartMode::ReadyToTaxi;
}

core_sim::PilotScenarioLocation make_geographic_scenario_location(
    const ScenarioSetup& setup) {
  return {
      "custom-geographic-position",
      "CUSTOM",
      "",
      "Custom geographic position",
      setup.latitude_deg,
      setup.longitude_deg,
      setup.altitude_m,
      setup.true_heading_deg,
      true,
  };
}

ScenarioStartResult start_scenario_from_setup(
    const ScenarioSetup& setup,
    const ScenarioAvailabilityInput& availability) {
  ScenarioStartResult result{};
  validate_setup(setup, result);

  const ScenarioAvailabilityReport availability_report =
      check_scenario_data_availability(availability);
  result.errors.insert(result.errors.end(),
                       availability_report.errors.begin(),
                       availability_report.errors.end());
  if (!result.errors.empty()) {
    return result;
  }

  ScenarioAvailabilityReport aircraft_report{};
  auto loaded_aircraft = load_aircraft_config(availability.aircraft_config_path, aircraft_report);
  result.errors.insert(result.errors.end(),
                       aircraft_report.errors.begin(),
                       aircraft_report.errors.end());
  if (loaded_aircraft.loaded &&
      loaded_aircraft.configuration.identity.model_name != setup.aircraft_id) {
    result.errors.push_back("selected aircraft is unavailable from aircraft configuration: " +
                            setup.aircraft_id);
  }
  if (!result.errors.empty()) {
    return result;
  }

  const core_sim::ScenarioSelection selection{
      setup.position_mode == ScenarioPositionMode::AirportOrRunway
          ? setup.location_id
          : std::string{"custom-geographic-position"},
      to_core_start_mode(setup.start_mode),
  };

  if (setup.position_mode == ScenarioPositionMode::GeographicPosition) {
    const core_sim::PilotScenarioLocation location =
        make_geographic_scenario_location(setup);
    result.initial_state =
        core_sim::make_scenario_initial_state(selection, std::span{&location, 1});
  } else {
    result.initial_state = core_sim::make_scenario_initial_state(selection);
  }

  try {
    apply_runtime_setup(setup, loaded_aircraft.configuration, result);
    apply_runtime_failures(setup, result);
  } catch (const std::exception& exception) {
    result.errors.push_back(exception.what());
  }
  if (!result.errors.empty()) {
    result.started = false;
    return result;
  }

  result.started = true;
  return result;
}

} // namespace flying::presentation
