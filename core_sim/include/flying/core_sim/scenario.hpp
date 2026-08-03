#pragma once

#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/simulator.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace flying::core_sim {

enum class ScenarioStartMode {
  ColdAndDark,
  ReadyToTaxi,
  Airborne,
};

struct PilotScenarioLocation {
  std::string location_id;
  std::string aerodrome_id;
  std::string runway_end_id;
  std::string display_name;
  double latitude_deg{};
  double longitude_deg{};
  double elevation_m{};
  double true_heading_deg{};
  bool selectable{true};
};

struct ScenarioSelection {
  std::string location_id{"FPPV-RWY-09"};
  ScenarioStartMode start_mode{ScenarioStartMode::ReadyToTaxi};
};

struct ScenarioInitialState {
  ScenarioSelection selection{};
  PilotScenarioLocation location{};
  AuthoritativeState rigid_body_state{};
  FlightDynamicsInitialCondition flight_dynamics_initial_condition{};
  AircraftControlInputSample initial_controls{};
  bool battery_on{};
  bool engine_running{};
  bool avionics_on{};
  bool parking_brake_set{};
};

[[nodiscard]] std::string_view to_string(ScenarioStartMode value) noexcept;
[[nodiscard]] ScenarioStartMode scenario_start_mode_from_string(std::string_view value);

[[nodiscard]] std::vector<PilotScenarioLocation> default_pilot_scenario_locations();

[[nodiscard]] ScenarioInitialState make_scenario_initial_state(
    const ScenarioSelection& selection);
[[nodiscard]] ScenarioInitialState make_scenario_initial_state(
    const ScenarioSelection& selection,
    std::span<const PilotScenarioLocation> locations);

[[nodiscard]] ScenarioInitialState reset_simulator_to_scenario(
    CoreSimulator& simulator,
    const ScenarioSelection& selection);
[[nodiscard]] ScenarioInitialState reset_simulator_to_scenario(
    CoreSimulator& simulator,
    const ScenarioSelection& selection,
    std::span<const PilotScenarioLocation> locations);

} // namespace flying::core_sim
