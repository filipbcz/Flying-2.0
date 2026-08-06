#include "flying/core_sim/aircraft_systems.hpp"

#include <cmath>
#include <stdexcept>

namespace {

using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::AircraftSystemsInput;
using flying::core_sim::AircraftSystemsModel;
using flying::core_sim::FlightDynamicsState;
using flying::core_sim::FuelTankSelector;
using flying::core_sim::PitotStaticSettings;
using flying::core_sim::qfe_pressure_for_field;

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

FlightDynamicsState make_truth(double altitude_m, double airspeed_mps) {
  FlightDynamicsState truth{};
  truth.altitude_m = altitude_m;
  truth.latitude_deg = 49.2;
  truth.longitude_deg = 16.6;
  truth.body_velocity_mps = {airspeed_mps, 0.0, 0.0};
  truth.ned_velocity_mps = {airspeed_mps, 0.0, 0.0};
  return truth;
}

AircraftSystemsInput make_input(double altitude_m, double airspeed_mps) {
  AircraftSystemsInput input{};
  input.truth = make_truth(altitude_m, airspeed_mps);
  input.controls.throttle_norm = 0.75;
  input.engine_rpm = 2'250.0;
  input.outside_air_temperature_k = 285.0;
  return input;
}

void pitot_static_lags_and_responds_to_atmosphere_and_altimeter_settings() {
  AircraftSystemsModel systems;
  systems.pitot_static().set_settings({101'325.0, 0.0});
  systems.pitot_static().reset(1'000.0, 40.0);

  const auto first = systems.step(0.1, make_input(1'000.0, 40.0));
  const auto changed = systems.step(0.1, make_input(1'400.0, 70.0));

  require(changed.indicated_altitude_m > first.indicated_altitude_m,
          "altimeter must respond to sensed static pressure changes");
  require(changed.indicated_altitude_m < 1'400.0,
          "altimeter must include sensor lag instead of direct truth altitude");
  require(changed.indicated_airspeed_mps > first.indicated_airspeed_mps,
          "airspeed must respond to sensed pitot pressure changes");
  require(changed.indicated_airspeed_mps < 70.0,
          "airspeed must include pitot sensor dynamics instead of direct truth speed");

  systems.pitot_static().set_settings({100'000.0, 0.0});
  const auto qnh_low = systems.step(1.0, make_input(1'400.0, 70.0));
  systems.pitot_static().set_settings({103'000.0, 0.0});
  const auto qnh_high = systems.step(1.0, make_input(1'400.0, 70.0));
  require(qnh_high.indicated_altitude_m > qnh_low.indicated_altitude_m,
          "altimeter indication must depend on QNH setting");

  systems.pitot_static().set_settings({101'325.0, 1'000.0});
  systems.pitot_static().reset(1'000.0, 40.0);
  const auto qfe = systems.step(1.0, make_input(1'000.0, 40.0));
  require_near(qfe.pitot_static.static_pressure_pa,
               qfe_pressure_for_field(1'000.0, 101'325.0),
               2'000.0,
               "QFE field pressure must be derived from the selected field elevation");
  require(std::abs(qfe.indicated_altitude_m) < 60.0,
          "QFE altimeter setting must indicate near field height at the reference field");
}

void pitot_icing_and_static_blockage_freeze_sensor_outputs() {
  AircraftSystemsModel systems;
  systems.pitot_static().reset(1'000.0, 45.0);

  const auto baseline = systems.step(1.0, make_input(1'000.0, 45.0));
  AircraftSystemsInput iced_input = make_input(1'000.0, 90.0);
  iced_input.icing_severity_norm = 1.0;
  const auto iced = systems.step(1.0, iced_input);
  require(iced.pitot_static.icing_present && iced.pitot_static.pitot_blocked,
          "pitot system must report icing-driven blockage when pitot heat is unavailable");
  require_near(iced.pitot_static.pitot_pressure_pa,
               baseline.pitot_static.pitot_pressure_pa,
               1.0,
               "iced pitot pressure must remain trapped instead of following truth speed");

  systems.failures().static_port_blocked = true;
  const auto blocked = systems.step(1.0, make_input(1'800.0, 45.0));
  require_near(blocked.pitot_static.static_pressure_pa,
               iced.pitot_static.static_pressure_pa,
               1.0,
               "blocked static pressure must remain trapped instead of following truth altitude");
}

void electrical_vacuum_and_gps_dependencies_are_stateful() {
  AircraftSystemsModel systems;
  auto input = make_input(1'000.0, 45.0);
  input.engine_rpm = 2'200.0;
  systems.switches().standby_vacuum_pump_on = true;

  auto powered = systems.step(5.0, input);
  require(powered.electrical.alternator_online, "alternator must come online with engine rpm");
  require(powered.electrical.avionics_bus_powered, "avionics bus must be powered by the electrical network");
  require(powered.gps.valid, "GPS must depend on avionics electrical power");
  require(powered.vacuum.suction_inhg > 3.8, "vacuum suction must build from an online pump");
  require(powered.engine.valid, "engine instruments must depend on electrical sensor power");

  systems.failures().alternator_failed = true;
  systems.failures().battery_failed = true;
  auto unpowered = systems.step(1.0, input);
  require(unpowered.electrical.bus_voltage_v == 0.0,
          "failed battery and alternator must remove bus voltage");
  require(!unpowered.gps.valid, "GPS must fail when avionics bus loses power");
  require(!unpowered.engine.valid, "engine instruments must fail when sensor power is lost");

  systems.failures().vacuum_pump_failed = true;
  systems.failures().standby_vacuum_pump_failed = true;
  auto vacuum_failed = systems.step(5.0, input);
  require(vacuum_failed.vacuum.suction_inhg < 3.8,
          "vacuum suction must decay when all vacuum sources fail");
}

void fuel_failures_affect_engine_instruments() {
  AircraftSystemsModel systems;
  systems.fuel().set_selector(FuelTankSelector::left);
  auto input = make_input(1'000.0, 45.0);
  input.controls.throttle_norm = 1.0;
  input.engine_rpm = 2'600.0;

  const auto normal = systems.step(1.0, input);
  require(normal.fuel.delivered_flow_kgps > 0.0, "fuel system must deliver requested engine flow");
  require(normal.engine.fuel_flow_kgps > 0.0, "engine fuel-flow instrument must use fuel sensor output");

  systems.failures().fuel_left_tank_blocked = true;
  systems.failures().engine_driven_fuel_pump_failed = true;
  systems.switches().electric_fuel_pump_on = false;
  const auto starved = systems.step(1.0, input);
  require(starved.fuel.engine_fuel_starved,
          "fuel tank blockage and pump failure must starve the engine");
  require(starved.engine.fuel_starved,
          "engine instruments must expose fuel starvation effects");
  require(starved.engine.rpm < normal.engine.rpm,
          "engine RPM indication must fall through the sensor model under starvation");
}

} // namespace

int main() {
  pitot_static_lags_and_responds_to_atmosphere_and_altimeter_settings();
  pitot_icing_and_static_blockage_freeze_sensor_outputs();
  electrical_vacuum_and_gps_dependencies_are_stateful();
  fuel_failures_affect_engine_instruments();
  return 0;
}
