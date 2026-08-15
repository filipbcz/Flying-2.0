#pragma once

#include "flying/core_sim/aircraft_systems.hpp"
#include "flying/core_sim/scenario.hpp"
#include "flying/core_sim/weather.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace flying::presentation {

enum class ScenarioStartMode {
  ColdAndDark,
  ReadyToTaxi,
  Airborne,
};

enum class ScenarioPositionMode {
  AirportOrRunway,
  GeographicPosition,
};

struct ScenarioFailureSelection {
  std::string failure_id;
  bool failed{};
};

struct ScenarioAvailabilityInput {
  std::filesystem::path terrain_manifest_path;
  std::filesystem::path visual_manifest_path;
  std::filesystem::path aircraft_config_path;
};

struct ScenarioAvailabilityReport {
  bool available{};
  std::vector<std::string> errors;
};

struct ScenarioRuntimeConfiguration {
  std::string aircraft_id;
  int local_year{};
  int local_month{};
  int local_day{};
  double local_time_seconds{};
  core_sim::WeatherScenario weather{};
  core_sim::AircraftMassBalanceState mass_balance{};
  core_sim::FailureStateModel failures{};
};

struct ScenarioSetup {
  std::string aircraft_id{"flying_trainer_one"};
  ScenarioPositionMode position_mode{ScenarioPositionMode::AirportOrRunway};
  std::string location_id{"FPPV-RWY-09"};
  double latitude_deg{49.2};
  double longitude_deg{14.5};
  double altitude_m{430.0};
  double true_heading_deg{90.0};
  int local_year{2026};
  int local_month{8};
  int local_day{7};
  double local_time_seconds{43200.0};
  core_sim::WeatherScenario weather{};
  double pilot_and_payload_weight_kg{180.0};
  double fuel_weight_kg{80.0};
  std::vector<ScenarioFailureSelection> failures;
  ScenarioStartMode start_mode{ScenarioStartMode::ReadyToTaxi};
};

struct ScenarioStartResult {
  bool started{};
  std::vector<std::string> errors;
  core_sim::ScenarioInitialState initial_state{};
  ScenarioRuntimeConfiguration runtime{};
};

[[nodiscard]] ScenarioAvailabilityReport check_scenario_data_availability(
    const ScenarioAvailabilityInput& input);

[[nodiscard]] core_sim::ScenarioStartMode to_core_start_mode(ScenarioStartMode mode) noexcept;

[[nodiscard]] core_sim::PilotScenarioLocation make_geographic_scenario_location(
    const ScenarioSetup& setup);

[[nodiscard]] ScenarioStartResult start_scenario_from_setup(
    const ScenarioSetup& setup,
    const ScenarioAvailabilityInput& availability);

} // namespace flying::presentation
