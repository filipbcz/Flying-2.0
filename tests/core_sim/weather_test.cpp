#include "flying/core_sim/aircraft_systems.hpp"
#include "flying/core_sim/simulator.hpp"
#include "flying/core_sim/terrain_contact.hpp"
#include "flying/core_sim/weather.hpp"

#include <cmath>
#include <stdexcept>

namespace {

using flying::core_sim::AircraftSystemsInput;
using flying::core_sim::AircraftSystemsModel;
using flying::core_sim::CoreSimulator;
using flying::core_sim::DisabledWeatherAdapter;
using flying::core_sim::DrydenTurbulenceSettings;
using flying::core_sim::FlightDynamicsState;
using flying::core_sim::TerrainContactData;
using flying::core_sim::Vector3d;
using flying::core_sim::WeatherScenario;
using flying::core_sim::apply_weather_to_terrain_contact;
using flying::core_sim::deterministic_dryden_turbulence;
using flying::core_sim::sample_weather;
using flying::geo_terrain::TerrainSurfaceMaterial;

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

double magnitude(Vector3d value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

WeatherScenario make_wet_icing_weather() {
  WeatherScenario scenario;
  scenario.scenario_id = "manual.test.wet-icing";
  scenario.qnh_pa = 99'500.0;
  scenario.sea_level_temperature_k = 274.15;
  scenario.relative_humidity_norm = 0.92;
  scenario.visibility_m = 8'000.0;
  scenario.surface_wind.wind_ned_mps = {6.0, -2.0, 0.0};
  scenario.wind_aloft = {1'000.0, {12.0, 4.0, 0.0}};
  scenario.turbulence = {2.0, 180.0, 42u};
  scenario.cloud = {700.0, 1'600.0, 0.8};
  scenario.precipitation = {6.0, 0.0, 0.4};
  return scenario;
}

FlightDynamicsState make_truth() {
  FlightDynamicsState truth{};
  truth.latitude_deg = 49.2;
  truth.longitude_deg = 16.6;
  truth.altitude_m = 900.0;
  truth.body_velocity_mps = {45.0, 0.0, 0.0};
  truth.ned_velocity_mps = {45.0, 0.0, 0.0};
  return truth;
}

void dryden_turbulence_is_deterministic_for_seeded_inputs() {
  const DrydenTurbulenceSettings settings{3.0, 250.0, 1234u};
  const Vector3d first = deterministic_dryden_turbulence(settings, 800.0, 42.0, 17.5);
  const Vector3d second = deterministic_dryden_turbulence(settings, 800.0, 42.0, 17.5);
  const Vector3d other_seed = deterministic_dryden_turbulence({3.0, 250.0, 4321u}, 800.0, 42.0, 17.5);

  require_near(first.x, second.x, 0.0, "Dryden turbulence north component must be deterministic");
  require_near(first.y, second.y, 0.0, "Dryden turbulence east component must be deterministic");
  require_near(first.z, second.z, 0.0, "Dryden turbulence down component must be deterministic");
  require(magnitude(first - other_seed) > 0.1,
          "Dryden turbulence must respond to fixed-seed changes");
}

void disabled_live_adapter_requires_explicit_implementation() {
  DisabledWeatherAdapter adapter;
  require(!adapter.enabled(), "default live weather adapter must be disabled");
  require(!adapter.load_for_location(49.2, 16.6, 0.0).has_value(),
          "default live weather adapter must not return METAR/GRIB data");
}

void core_sim_receives_weather_without_visuals() {
  CoreSimulator simulator;
  simulator.set_manual_weather_scenario(make_wet_icing_weather());
  const auto report = simulator.advance(simulator.fixed_step_s(), {});
  const auto& weather = simulator.weather();

  require(report.steps_executed == 1, "simulator must still advance with weather enabled");
  require(weather.atmosphere.static_pressure_pa < 101'325.0,
          "CoreSim weather must expose numerical pressure");
  require(weather.atmosphere.density_kgpm3 > 0.0,
          "CoreSim weather must expose numerical density");
  require(weather.atmosphere.relative_humidity_norm > 0.9,
          "CoreSim weather must expose numerical humidity");
  require(magnitude(weather.wind_ned_mps) > 1.0,
          "CoreSim weather must expose numerical wind");
  require(simulator.state().weather_dynamic_pressure_pa > 0.0,
          "CoreSim must compute relative-air dynamic pressure from wind and density");
}

void weather_effects_reach_aircraft_sensors_and_surfaces() {
  const auto weather = sample_weather(make_wet_icing_weather(), 49.2, 16.6, 900.0, 10.0);
  AircraftSystemsModel systems;
  AircraftSystemsInput input;
  input.truth = make_truth();
  input.controls.throttle_norm = 0.8;
  input.engine_rpm = 2'400.0;
  input.weather = weather;
  input.weather_valid = true;

  const auto instruments = systems.step(1.0, input);
  require(instruments.weather.visibility_m < 8'000.0,
          "clouds and precipitation must reduce the numerical visibility feed");
  require(instruments.pitot_static.icing_present,
          "icing weather must affect pitot-static sensor behavior");
  require(instruments.engine.rpm < 2'400.0,
          "icing or precipitation must impose an engine performance effect");

  TerrainContactData dry_contact;
  dry_contact.surface_material = TerrainSurfaceMaterial::kAsphalt;
  dry_contact.runway_override_active = true;
  const TerrainContactData wet_contact = apply_weather_to_terrain_contact(dry_contact, weather);
  require(wet_contact.weather_friction_scale < 1.0,
          "wet surfaces must reduce runway friction scale");
}

} // namespace

int main() {
  dryden_turbulence_is_deterministic_for_seeded_inputs();
  disabled_live_adapter_requires_explicit_implementation();
  core_sim_receives_weather_without_visuals();
  weather_effects_reach_aircraft_sensors_and_surfaces();
  return 0;
}
