#pragma once

#include "flying/core_sim/aircraft_systems.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace flying::presentation {

enum class CockpitControlKind {
  Toggle,
  Momentary,
  Axis,
  Selector,
};

enum class CockpitWorkflowPhase {
  EngineStart,
  Taxi,
  Takeoff,
  Flight,
  Landing,
  Shutdown,
};

struct CockpitControlDefinition {
  std::string id;
  std::string label;
  CockpitControlKind kind{};
  double minimum{};
  double maximum{1.0};
  std::vector<CockpitWorkflowPhase> required_for;
};

struct CockpitControlState {
  std::string id;
  double value{};
  bool operable{true};
};

struct CockpitInstrumentDisplay {
  std::string id;
  std::string label;
  double value{};
  double secondary_value{};
  std::string unit;
  std::string secondary_unit;
  std::uint64_t sensor_sequence{};
  bool sensor_driven{};
  bool valid{true};
};

struct CockpitLightingState {
  double instrument_backlight_norm{};
  double flood_light_norm{};
  double label_readability_score{};
  bool night_mode{};
};

struct CockpitEngineSoundHook {
  std::string cue_id{"engine_piston_loop"};
  double rpm{};
  double throttle_norm{};
  double fuel_flow_kgps{};
  bool active{};
};

struct CockpitScreenshotComparisonBaseline {
  std::string id;
  std::string view;
  std::filesystem::path image_path;
  double maximum_difference_percent{1.0};
};

struct CockpitRasterImage {
  int width{};
  int height{};
  std::vector<std::uint8_t> rgba;
};

struct CockpitScreenshotComparisonResult {
  std::string id;
  double difference_percent{};
  bool passed{};
};

struct CockpitInstrumentBinding {
  std::string id;
  std::string source;
  std::string animation;
  std::string units;
  std::vector<std::string> channels;
};

struct CockpitControlBinding {
  std::string id;
  std::string binding;
  std::string kind;
  std::vector<std::string> required_for;
};

struct CockpitAudioHookBinding {
  std::string id;
  std::string source;
  std::vector<std::string> parameters;
};

struct CockpitLayout {
  std::string schema_version;
  std::string aircraft_id;
  std::filesystem::path screenshot_capture_provenance_path;
  std::string screenshot_capture_command;
  std::vector<std::string> night_view_states;
  std::vector<CockpitControlBinding> controls;
  std::vector<CockpitInstrumentBinding> instruments;
  std::vector<CockpitAudioHookBinding> audio_hooks;
  std::vector<CockpitScreenshotComparisonBaseline> screenshot_baselines;
  double minimum_night_readability_score{};
  int minimum_pixels_per_capital_height{};
  int minimum_screenshot_width{};
  int minimum_screenshot_height{};
  bool casts_panel_shadows{};
};

struct CockpitLayoutLoadResult {
  bool loaded{};
  CockpitLayout layout;
  std::vector<std::string> errors;
};

struct CockpitAircraftCommandState {
  core_sim::AircraftControlInputSample aircraft_controls{};
  core_sim::AircraftSystemsSwitches system_switches{};
  core_sim::FuelTankSelector fuel_selector{core_sim::FuelTankSelector::both};
  bool parking_brake_set{};
  bool landing_light_on{};
  bool taxi_light_on{};
  bool navigation_lights_on{};
  bool beacon_light_on{};
};

struct CockpitPresentationFrame {
  std::vector<CockpitControlState> controls;
  std::vector<CockpitInstrumentDisplay> instruments;
  CockpitLightingState lighting;
  CockpitEngineSoundHook engine_sound;
  CockpitAircraftCommandState commands;
};

class CockpitSubsystem {
public:
  CockpitSubsystem();

  [[nodiscard]] const std::vector<CockpitControlDefinition>& controls() const noexcept;
  [[nodiscard]] const CockpitPresentationFrame& frame() const noexcept;
  [[nodiscard]] const CockpitAircraftCommandState& command_state() const noexcept;

  [[nodiscard]] bool set_control_value(const std::string& id, double value);
  [[nodiscard]] bool control_is_operable_for(CockpitWorkflowPhase phase) const;
  [[nodiscard]] bool has_required_controls_for_all_workflow_phases() const;

  CockpitPresentationFrame update_from_instruments(
      const core_sim::InstrumentData& instruments,
      double local_time_seconds);

private:
  void dispatch_control_to_aircraft(const CockpitControlDefinition& control, double value);

  std::vector<CockpitControlDefinition> controls_;
  CockpitPresentationFrame frame_{};
  std::map<std::string, std::size_t> control_index_by_id_;
};

[[nodiscard]] std::vector<CockpitControlDefinition> make_default_cockpit_controls();

[[nodiscard]] bool cockpit_phase_requires_control(const CockpitControlDefinition& control,
                                                  CockpitWorkflowPhase phase);

[[nodiscard]] CockpitScreenshotComparisonResult compare_cockpit_screenshot_state(
    const CockpitScreenshotComparisonBaseline& baseline,
    const CockpitRasterImage& expected,
    const CockpitRasterImage& captured);

[[nodiscard]] CockpitLayoutLoadResult load_cockpit_layout(const std::filesystem::path& path);

[[nodiscard]] CockpitRasterImage load_cockpit_ppm_screenshot(const std::filesystem::path& path);

} // namespace flying::presentation
