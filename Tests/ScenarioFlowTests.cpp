#include "CameraSubsystem.h"
#include "ScenarioSubsystem.h"

#include "flying/geo_terrain/geodesy.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

std::filesystem::path repo_path(const char* relative_path) {
  return std::filesystem::path(FLYING_REPO_SOURCE_DIR) / relative_path;
}

flying::presentation::ScenarioAvailabilityInput valid_availability() {
  return {
      repo_path("Data/Terrain/SliceManifest.json"),
      repo_path("Data/Visual/SliceManifest.json"),
      repo_path("core_sim/aircraft/flying_trainer_one/aircraft-config.json"),
  };
}

double altitude_m(const flying::core_sim::ScenarioInitialState& state) {
  const flying::geo_terrain::EcefPosition ecef{
      {state.rigid_body_state.ecef_position_m.x,
       state.rigid_body_state.ecef_position_m.y,
       state.rigid_body_state.ecef_position_m.z}};
  return flying::geo_terrain::ecef_to_geodetic(ecef).ellipsoidal_height.meters;
}

void unavailable_data_blocks_start_with_clear_errors() {
  const flying::presentation::ScenarioAvailabilityInput missing{
      repo_path("Data/Terrain/missing-terrain.json"),
      repo_path("Data/Visual/missing-visual.json"),
      repo_path("core_sim/aircraft/missing/aircraft-config.json"),
  };

  const auto availability =
      flying::presentation::check_scenario_data_availability(missing);
  require(!availability.available, "missing required data must be unavailable");
  require(availability.errors.size() == 3,
          "terrain, visual, and aircraft failures must each be reported");

  flying::presentation::ScenarioSetup setup{};
  const auto start = flying::presentation::start_scenario_from_setup(setup, missing);
  require(!start.started, "scenario start must fail when required data is unavailable");
  require(start.errors.size() == 3, "scenario start must return clear availability errors");

  flying::presentation::ScenarioSetup wrong_aircraft{};
  wrong_aircraft.aircraft_id = "unavailable_selected_aircraft";
  const auto aircraft_mismatch =
      flying::presentation::start_scenario_from_setup(wrong_aircraft, valid_availability());
  require(!aircraft_mismatch.started,
          "scenario start must fail when selected aircraft does not match available config");
}

void start_modes_initialize_distinct_validated_states() {
  flying::presentation::ScenarioSetup setup{};
  setup.position_mode = flying::presentation::ScenarioPositionMode::AirportOrRunway;
  setup.location_id = "FPPV-RWY-09";

  setup.start_mode = flying::presentation::ScenarioStartMode::ColdAndDark;
  const auto cold =
      flying::presentation::start_scenario_from_setup(setup, valid_availability());
  require(cold.started, "cold-and-dark start must pass with valid data");
  require(!cold.initial_state.battery_on, "cold-and-dark must start with battery off");
  require(!cold.initial_state.engine_running, "cold-and-dark must start with engine stopped");
  require(cold.initial_state.parking_brake_set, "cold-and-dark must set parking brake");
  require_near(cold.initial_state.initial_controls.throttle_norm,
               0.0,
               1.0e-12,
               "cold-and-dark throttle must be closed");

  setup.start_mode = flying::presentation::ScenarioStartMode::ReadyToTaxi;
  const auto taxi =
      flying::presentation::start_scenario_from_setup(setup, valid_availability());
  require(taxi.started, "ready-to-taxi start must pass with valid data");
  require(taxi.initial_state.battery_on, "ready-to-taxi must start with battery on");
  require(taxi.initial_state.engine_running, "ready-to-taxi must start engine running");
  require(!taxi.initial_state.avionics_on, "ready-to-taxi must keep avionics distinct");
  require_near(taxi.initial_state.initial_controls.throttle_norm,
               0.05,
               1.0e-12,
               "ready-to-taxi throttle must be idle");

  setup.start_mode = flying::presentation::ScenarioStartMode::Airborne;
  const auto airborne =
      flying::presentation::start_scenario_from_setup(setup, valid_availability());
  require(airborne.started, "airborne start must pass with valid data");
  require(airborne.initial_state.battery_on, "airborne must start with battery on");
  require(airborne.initial_state.engine_running, "airborne must start engine running");
  require(airborne.initial_state.avionics_on, "airborne must start avionics on");
  require(airborne.initial_state.initial_controls.throttle_norm >
              taxi.initial_state.initial_controls.throttle_norm,
          "airborne throttle must be distinct from taxi idle");
  require(altitude_m(airborne.initial_state) > altitude_m(taxi.initial_state) + 400.0,
          "airborne start must initialize above the runway start state");
}

void scenario_setup_supports_geographic_weather_mass_fuel_and_failures() {
  flying::presentation::ScenarioSetup setup{};
  setup.aircraft_id = "flying_trainer_one";
  setup.position_mode = flying::presentation::ScenarioPositionMode::GeographicPosition;
  setup.latitude_deg = 49.42;
  setup.longitude_deg = 15.03;
  setup.altitude_m = 510.0;
  setup.true_heading_deg = 135.0;
  setup.local_year = 2026;
  setup.local_month = 8;
  setup.local_day = 7;
  setup.local_time_seconds = 15.0 * 3600.0;
  setup.weather.qnh_pa = 100900.0;
  setup.pilot_and_payload_weight_kg = 176.0;
  setup.fuel_weight_kg = 64.0;
  setup.failures.push_back({"pitot_blocked", true});
  setup.start_mode = flying::presentation::ScenarioStartMode::ReadyToTaxi;

  const auto start =
      flying::presentation::start_scenario_from_setup(setup, valid_availability());
  require(start.started, "geographic scenario setup must start with valid data");
  require(start.initial_state.location.location_id == "custom-geographic-position",
          "geographic scenarios must use an explicit generated location");
  require_near(start.initial_state.location.latitude_deg,
               setup.latitude_deg,
               1.0e-12,
               "geographic latitude must transfer to initial state");
  require_near(start.initial_state.location.longitude_deg,
               setup.longitude_deg,
               1.0e-12,
               "geographic longitude must transfer to initial state");
  require(start.runtime.aircraft_id == setup.aircraft_id,
          "selected aircraft id must be recorded in runtime state");
  require(start.runtime.local_year == setup.local_year &&
              start.runtime.local_month == setup.local_month &&
              start.runtime.local_day == setup.local_day,
          "selected local date must be recorded in runtime state");
  require_near(start.runtime.local_time_seconds,
               setup.local_time_seconds,
               1.0e-12,
               "selected local time must be recorded in runtime state");
  require_near(start.runtime.weather.qnh_pa,
               setup.weather.qnh_pa,
               1.0e-12,
               "selected weather scenario must be recorded in runtime state");
  require_near(start.runtime.mass_balance.fuel_mass_kg,
               setup.fuel_weight_kg,
               1.0e-9,
               "selected fuel mass must be applied to runtime mass balance");
  require_near(start.runtime.mass_balance.payload_mass_kg,
               setup.pilot_and_payload_weight_kg,
               1.0e-9,
               "selected pilot and payload mass must be applied to runtime mass balance");
  require_near(start.initial_state.rigid_body_state.aircraft_mass_balance.fuel_mass_kg,
               setup.fuel_weight_kg,
               1.0e-9,
               "selected fuel mass must be applied to authoritative initial state");
  const auto expected_weather =
      flying::core_sim::sample_weather(setup.weather,
                                       start.initial_state.location.latitude_deg,
                                       start.initial_state.location.longitude_deg,
                                       start.initial_state.flight_dynamics_initial_condition.altitude_m,
                                       setup.local_time_seconds);
  require_near(start.initial_state.rigid_body_state.weather.atmosphere.static_pressure_pa,
               expected_weather.atmosphere.static_pressure_pa,
               1.0e-9,
               "selected weather must be sampled into authoritative initial state");
  require(start.runtime.failures.pitot_blocked,
          "selected pitot failure must be applied to runtime failures");

  setup.failures.push_back({"unsupported_failure_id", true});
  const auto unsupported_failure =
      flying::presentation::start_scenario_from_setup(setup, valid_availability());
  require(!unsupported_failure.started,
          "unsupported failure selections must clearly block scenario start");
}

void cockpit_and_external_cameras_share_authoritative_state() {
  flying::core_sim::AuthoritativeState state{};
  state.step_index = 42;
  state.ecef_position_m = {1.0, 2.0, 3.0};
  state.body_to_ecef = {1.0, 0.0, 0.0, 0.0};

  const auto cockpit =
      flying::presentation::make_camera_view(flying::presentation::CameraMode::Cockpit, state);
  const auto external =
      flying::presentation::make_camera_view(flying::presentation::CameraMode::External, state);

  require(cockpit.mode != external.mode, "camera modes must be distinct");
  require(cockpit.camera_offset_body_m.x != external.camera_offset_body_m.x,
          "camera offsets must be distinct");
  require(flying::presentation::camera_views_share_authoritative_aircraft_state(cockpit, external),
          "cockpit and external cameras must display the same authoritative state");
}

} // namespace

int main() {
  unavailable_data_blocks_start_with_clear_errors();
  start_modes_initialize_distinct_validated_states();
  scenario_setup_supports_geographic_weather_mass_fuel_and_failures();
  cockpit_and_external_cameras_share_authoritative_state();
  return 0;
}
