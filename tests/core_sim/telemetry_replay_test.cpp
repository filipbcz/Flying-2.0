#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/telemetry.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

using flying::core_sim::AdvanceReport;
using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::AuthoritativeState;
using flying::core_sim::CallerFrameInput;
using flying::core_sim::ControlInputSample;
using flying::core_sim::CoreSimulator;
using flying::core_sim::EngineStateSample;
using flying::core_sim::ReplayCompatibilityPolicy;
using flying::core_sim::ReplayEnvironment;
using flying::core_sim::RigidBodyParameters;
using flying::core_sim::TelemetryRecording;
using flying::core_sim::TelemetryRecorder;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

AuthoritativeState initial_state() {
  AuthoritativeState state{};
  state.ecef_position_m = {3'980'000.0, 1'100'000.0, 4'870'000.0};
  state.ecef_velocity_mps = {32.0, 0.0, -0.2};
  return state;
}

AircraftControlInputSample scripted_aircraft_controls(std::size_t frame_index) {
  AircraftControlInputSample controls{};
  controls.aileron_norm = frame_index % 2 == 0 ? 0.15 : -0.10;
  controls.elevator_norm = -0.05;
  controls.rudder_norm = 0.02;
  controls.throttle_norm = 0.72;
  controls.flaps_norm = frame_index > 2 ? 0.10 : 0.0;
  controls.brake_left_norm = 0.0;
  controls.brake_right_norm = 0.0;
  controls.mixture_norm = 1.0;
  controls.propeller_norm = 0.95;
  return controls;
}

TelemetryRecording make_recording() {
  const RigidBodyParameters parameters{
    975.0,
    {875.0, 1'060.0, 1'420.0},
  };

  CoreSimulator simulator{parameters};
  simulator.reset(initial_state());

  auto metadata = flying::core_sim::make_default_telemetry_metadata();
  metadata.session_id = "telemetry-replay-test";
  metadata.started_unix_ms = 1'700'000'000'000;
  metadata.scenario_location_id = "FPPV-RWY-09";
  metadata.scenario_start_mode = "airborne";
  metadata.data_packages.push_back({"pilot-region-fixtures", "v1"});

  AircraftControlInputSample initial_controls{};
  initial_controls.throttle_norm = 0.72;
  initial_controls.mixture_norm = 1.0;
  initial_controls.propeller_norm = 0.95;

  TelemetryRecorder recorder{metadata, simulator.state(), {}, initial_controls, parameters};

  const std::array<CallerFrameInput, 6> input_stream{{
    {1.0 / 480.0, ControlInputSample{{1'100.0, 0.0, 80.0}, {0.0, 18.0, 0.0}}},
    {1.0 / 480.0, ControlInputSample{{1'100.0, 0.0, 80.0}, {0.0, 18.0, 0.0}}},
    {1.0 / 120.0, ControlInputSample{{1'250.0, 10.0, 75.0}, {2.0, 16.0, -4.0}}},
    {1.0 / 240.0, ControlInputSample{{1'300.0, -12.0, 70.0}, {-3.0, 14.0, -6.0}}},
    {1.0 / 80.0, ControlInputSample{{1'420.0, -18.0, 65.0}, {-6.0, 12.0, -8.0}}},
    {0.004, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
  }};

  for (std::size_t frame_index = 0; frame_index < input_stream.size(); ++frame_index) {
    const CallerFrameInput& frame = input_stream[frame_index];
    const AdvanceReport report = simulator.advance(frame.elapsed_time_s, frame.controls);
    const AircraftControlInputSample aircraft_controls = scripted_aircraft_controls(frame_index);
    const EngineStateSample engine =
        flying::core_sim::make_engine_state_sample(aircraft_controls, true);
    recorder.record_advance(frame.elapsed_time_s,
                            frame.controls,
                            aircraft_controls,
                            engine,
                            report,
                            simulator.state(),
                            1'700'000'000'100 + static_cast<std::int64_t>(frame_index));
  }

  return recorder.recording();
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void remove_temp_file(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

void telemetry_file_round_trips_and_replays_deterministically() {
  const std::filesystem::path telemetry_path =
      std::filesystem::temp_directory_path() / "flying-telemetry-replay-test.flytelem";
  remove_temp_file(telemetry_path);

  const TelemetryRecording recording = make_recording();
  require(flying::core_sim::validate_telemetry_recording(recording).empty(),
          "constructed telemetry recording must validate");

  const auto saved = flying::core_sim::save_telemetry_file_atomic(telemetry_path, recording);
  require(saved.saved, "telemetry file must save");

  const auto loaded = flying::core_sim::load_telemetry_file(telemetry_path);
  require(loaded.loaded, "saved telemetry file must load");
  require(loaded.recording.frames.size() == recording.frames.size(),
          "telemetry frames must round-trip");
  require(loaded.recording.metadata.core_sim_version == recording.metadata.core_sim_version,
          "CoreSim version metadata must round-trip");
  require(loaded.recording.metadata.data_packages.size() == recording.metadata.data_packages.size(),
          "data package versions must round-trip");

  ReplayEnvironment environment = flying::core_sim::make_current_replay_environment();
  environment.rigid_body_parameters = recording.rigid_body_parameters;
  environment.data_packages.push_back({"pilot-region-fixtures", "v1"});
  CoreSimulator replay_simulator{RigidBodyParameters{975.0, {875.0, 1'060.0, 1'420.0}}};
  const auto replay = flying::core_sim::replay_recording(
      loaded.recording,
      replay_simulator,
      environment,
      ReplayCompatibilityPolicy::RefuseOnMismatch);
  require(replay.played, "compatible telemetry replay must play");
  require(replay.deterministic, "compatible telemetry replay must reproduce state hashes");
  require(replay.final_state_hash == recording.frames.back().state_hash,
          "final replay state hash must match the recording");

  remove_temp_file(telemetry_path);
}

void exports_include_validation_metadata_and_flight_state() {
  const std::filesystem::path csv_path =
      std::filesystem::temp_directory_path() / "flying-telemetry-export-test.csv";
  const std::filesystem::path json_path =
      std::filesystem::temp_directory_path() / "flying-telemetry-export-test.json";
  remove_temp_file(csv_path);
  remove_temp_file(json_path);

  TelemetryRecording recording = make_recording();
  recording.metadata.session_id = "=telemetry-validation";
  const auto csv = flying::core_sim::export_telemetry_csv(csv_path, recording);
  const auto json = flying::core_sim::export_telemetry_json(json_path, recording);
  require(csv.exported, "CSV telemetry export must succeed");
  require(json.exported, "JSON telemetry export must succeed");

  const std::string csv_text = read_text(csv_path);
  require(csv_text.find("flying.telemetry-export.csv.v1") != std::string::npos,
          "CSV export must identify its schema version");
  require(csv_text.find("core_sim_version") != std::string::npos,
          "CSV export must include CoreSim version metadata");
  require(csv_text.find("# core_sim_version,\"") != std::string::npos,
          "CSV export must quote string metadata values");
  require(csv_text.find("# session_id,\"'=telemetry-validation\"") != std::string::npos,
          "CSV export must neutralize spreadsheet formulas in string metadata");
  require(csv_text.find("ecef_x_m") != std::string::npos,
          "CSV export must include flight path position columns");
  require(csv_text.find("input_force_x_n") != std::string::npos,
          "CSV export must include force input columns");
  require(csv_text.find("engine_throttle_norm") != std::string::npos,
          "CSV export must include engine state columns");

  const std::string json_text = read_text(json_path);
  require(json_text.find("\"schema_version\":\"flying.telemetry-export.json.v1\"") !=
              std::string::npos,
          "JSON export must identify its schema version");
  require(json_text.find("\"flight_path\"") != std::string::npos,
          "JSON export must include a flight path collection");
  require(json_text.find("\"aircraft_controls\"") != std::string::npos,
          "JSON export must include aircraft control inputs");
  require(json_text.find("\"engine\"") != std::string::npos,
          "JSON export must include engine states");
  require(json_text.find("\"data_packages\"") != std::string::npos,
          "JSON export must include data package versions");

  remove_temp_file(csv_path);
  remove_temp_file(json_path);
}

void incompatible_versions_refuse_or_warn_before_replay() {
  const TelemetryRecording recording = make_recording();
  ReplayEnvironment incompatible = flying::core_sim::make_current_replay_environment();
  incompatible.core_sim_version = "0.0-incompatible";
  incompatible.rigid_body_parameters = recording.rigid_body_parameters;
  incompatible.data_packages.push_back({"pilot-region-fixtures", "v1"});

  CoreSimulator refused_simulator{RigidBodyParameters{975.0, {875.0, 1'060.0, 1'420.0}}};
  const auto refused = flying::core_sim::replay_recording(
      recording,
      refused_simulator,
      incompatible,
      ReplayCompatibilityPolicy::RefuseOnMismatch);
  require(refused.refused, "CoreSim version mismatch must refuse replay under the strict policy");
  require(!refused.errors.empty(), "strict replay refusal must report compatibility errors");

  CoreSimulator warned_simulator{RigidBodyParameters{975.0, {875.0, 1'060.0, 1'420.0}}};
  const auto warned = flying::core_sim::replay_recording(
      recording,
      warned_simulator,
      incompatible,
      ReplayCompatibilityPolicy::WarnOnMismatch);
  require(warned.played, "warning policy must allow replay after surfacing incompatibility");
  require(!warned.warnings.empty(), "warning policy must surface version incompatibility warnings");
}

} // namespace

int main() {
  telemetry_file_round_trips_and_replays_deterministically();
  exports_include_validation_metadata_and_flight_state();
  incompatible_versions_refuse_or_warn_before_replay();
  return 0;
}
