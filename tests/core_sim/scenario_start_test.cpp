#include "flying/core_sim/scenario.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using flying::core_sim::CoreSimulator;
using flying::core_sim::ScenarioSelection;
using flying::core_sim::ScenarioStartMode;

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

double speed_magnitude(const flying::core_sim::Vector3d& value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

void pilot_region_catalog_exposes_selectable_runway_locations() {
  const auto locations = flying::core_sim::default_pilot_scenario_locations();
  require(locations.size() == 4, "pilot scenario catalog must expose the pilot runway ends");
  require(locations.front().location_id == "FPPV-RWY-09",
          "default pilot scenario catalog must start at the paved pilot airport");
  for (const auto& location : locations) {
    require(location.selectable, "pilot scenario location must be selectable");
    require(!location.aerodrome_id.empty(), "pilot scenario location must reference an aerodrome");
    require(!location.runway_end_id.empty(), "pilot scenario location must reference a runway end");
  }
}

void start_modes_create_distinct_aircraft_states() {
  const auto cold = flying::core_sim::make_scenario_initial_state(
      ScenarioSelection{"FPPV-RWY-09", ScenarioStartMode::ColdAndDark});
  const auto taxi = flying::core_sim::make_scenario_initial_state(
      ScenarioSelection{"FPPV-RWY-09", ScenarioStartMode::ReadyToTaxi});
  const auto airborne = flying::core_sim::make_scenario_initial_state(
      ScenarioSelection{"FPPV-RWY-09", ScenarioStartMode::Airborne});

  require(!cold.battery_on, "cold-and-dark scenario must start with battery off");
  require(!cold.engine_running, "cold-and-dark scenario must start with engine stopped");
  require(cold.parking_brake_set, "cold-and-dark scenario must start parked");
  require_near(cold.initial_controls.mixture_norm, 0.0, 0.0,
               "cold-and-dark scenario must start at mixture cutoff");
  require_near(speed_magnitude(cold.rigid_body_state.ecef_velocity_mps), 0.0, 1.0e-12,
               "cold-and-dark scenario must start stationary");

  require(taxi.battery_on, "ready-to-taxi scenario must start electrically live");
  require(taxi.engine_running, "ready-to-taxi scenario must start with engine running");
  require(!taxi.parking_brake_set, "ready-to-taxi scenario must be able to roll");
  require_near(taxi.initial_controls.mixture_norm, 1.0, 0.0,
               "ready-to-taxi scenario must start with mixture rich");

  require(airborne.battery_on, "airborne scenario must start electrically live");
  require(airborne.engine_running, "airborne scenario must start with engine running");
  require(airborne.avionics_on, "airborne scenario must start with avionics on");
  require_near(airborne.flight_dynamics_initial_condition.altitude_m,
               airborne.location.elevation_m + 450.0,
               1.0e-12,
               "airborne scenario must start above the selected pilot location");
  require_near(speed_magnitude(airborne.rigid_body_state.ecef_velocity_mps), 35.0, 1.0e-12,
               "airborne scenario must hand off an initial airspeed");
}

void scenario_reset_hands_initial_state_to_core_sim() {
  CoreSimulator simulator;
  const auto initial = flying::core_sim::reset_simulator_to_scenario(
      simulator,
      ScenarioSelection{"FPGS-RWY-16", ScenarioStartMode::Airborne});

  require_near(simulator.state().ecef_position_m.x, initial.rigid_body_state.ecef_position_m.x, 0.0,
               "CoreSim reset must receive scenario ECEF position");
  require_near(simulator.state().ecef_velocity_mps.y, initial.rigid_body_state.ecef_velocity_mps.y, 0.0,
               "CoreSim reset must receive scenario ECEF velocity");
  require_near(simulator.state().body_to_ecef.w, initial.rigid_body_state.body_to_ecef.w, 0.0,
               "CoreSim reset must receive scenario orientation");
  require(simulator.state().step_index == 0, "CoreSim scenario reset must reset the step index");
  require_near(simulator.flight_dynamics_initial_condition().latitude_deg,
               initial.flight_dynamics_initial_condition.latitude_deg,
               0.0,
               "CoreSim reset must receive the scenario flight-dynamics latitude");
  require_near(simulator.flight_dynamics_initial_condition().altitude_m,
               initial.flight_dynamics_initial_condition.altitude_m,
               0.0,
               "CoreSim reset must receive the scenario flight-dynamics altitude");
  require_near(simulator.initial_aircraft_controls().throttle_norm,
               initial.initial_controls.throttle_norm,
               0.0,
               "CoreSim reset must receive the scenario throttle setting");
  require_near(simulator.initial_aircraft_controls().mixture_norm,
               initial.initial_controls.mixture_norm,
               0.0,
               "CoreSim reset must receive the scenario mixture setting");

  CoreSimulator cold_simulator;
  const auto cold_reset = flying::core_sim::reset_simulator_to_scenario(
      cold_simulator,
      ScenarioSelection{"FPPV-RWY-09", ScenarioStartMode::ColdAndDark});
  CoreSimulator taxi_simulator;
  const auto taxi_reset = flying::core_sim::reset_simulator_to_scenario(
      taxi_simulator,
      ScenarioSelection{"FPPV-RWY-09", ScenarioStartMode::ReadyToTaxi});
  require(cold_reset.selection.start_mode == ScenarioStartMode::ColdAndDark,
          "cold-and-dark reset must return the requested start mode");
  require(taxi_reset.selection.start_mode == ScenarioStartMode::ReadyToTaxi,
          "ready-to-taxi reset must return the requested start mode");
  require_near(cold_simulator.initial_aircraft_controls().mixture_norm, 0.0, 0.0,
               "cold-and-dark reset must hand mixture cutoff to CoreSim");
  require_near(taxi_simulator.initial_aircraft_controls().throttle_norm, 0.05, 0.0,
               "ready-to-taxi reset must hand taxi throttle to CoreSim");
}

void invalid_scenario_selection_is_rejected() {
  bool threw = false;
  try {
    (void)flying::core_sim::make_scenario_initial_state(
        ScenarioSelection{"NOT-A-PILOT-LOCATION", ScenarioStartMode::ReadyToTaxi});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  require(threw, "unknown scenario locations must be rejected");
}

} // namespace

int main() {
  pilot_region_catalog_exposes_selectable_runway_locations();
  start_modes_create_distinct_aircraft_states();
  scenario_reset_hands_initial_state_to_core_sim();
  invalid_scenario_selection_is_rejected();
  return 0;
}
