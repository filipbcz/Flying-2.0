#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/telemetry.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using flying::core_sim::AdvanceReport;
using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::AircraftInertiaTensor;
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
using flying::core_sim::Vector3d;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

[[nodiscard]] std::uint64_t append_legacy_u64(std::uint64_t hash, std::uint64_t value) noexcept {
  constexpr std::uint64_t kLegacyFnvPrime = 1'099'511'628'211ull;
  for (int byte_index = 0; byte_index < 8; ++byte_index) {
    const auto byte = static_cast<std::uint8_t>((value >> (byte_index * 8)) & 0xffu);
    hash ^= byte;
    hash *= kLegacyFnvPrime;
  }
  return hash;
}

[[nodiscard]] std::uint64_t legacy_double_bits(double value) noexcept {
  if (value == 0.0) {
    value = 0.0;
  }
  if (std::isnan(value)) {
    return 0x7ff8'0000'0000'0000ull;
  }
  return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint64_t append_legacy_double(std::uint64_t hash, double value) noexcept {
  return append_legacy_u64(hash, legacy_double_bits(value));
}

[[nodiscard]] std::uint64_t append_legacy_vector(std::uint64_t hash, Vector3d value) noexcept {
  hash = append_legacy_double(hash, value.x);
  hash = append_legacy_double(hash, value.y);
  return append_legacy_double(hash, value.z);
}

[[nodiscard]] std::uint64_t append_legacy_quaternion(std::uint64_t hash,
                                                     flying::core_sim::Quaterniond value) noexcept {
  hash = append_legacy_double(hash, value.w);
  hash = append_legacy_double(hash, value.x);
  hash = append_legacy_double(hash, value.y);
  return append_legacy_double(hash, value.z);
}

[[nodiscard]] std::uint64_t append_legacy_inertia_tensor(
    std::uint64_t hash,
    AircraftInertiaTensor value) noexcept {
  hash = append_legacy_double(hash, value.ixx);
  hash = append_legacy_double(hash, value.iyy);
  hash = append_legacy_double(hash, value.izz);
  hash = append_legacy_double(hash, value.ixy);
  hash = append_legacy_double(hash, value.ixz);
  return append_legacy_double(hash, value.iyz);
}

[[nodiscard]] std::uint64_t hash_legacy_state(const AuthoritativeState& state) noexcept {
  constexpr std::uint64_t kLegacyFnvOffset = 14'695'981'039'346'656'037ull;
  constexpr std::uint64_t kLegacyHashSchemaVersion = 2;
  auto hash = append_legacy_u64(kLegacyFnvOffset, kLegacyHashSchemaVersion);
  hash = append_legacy_double(hash, state.simulation_time_s);
  hash = append_legacy_u64(hash, state.step_index);
  hash = append_legacy_vector(hash, state.ecef_position_m);
  hash = append_legacy_vector(hash, state.ecef_velocity_mps);
  hash = append_legacy_quaternion(hash, state.body_to_ecef);
  hash = append_legacy_vector(hash, state.angular_velocity_body_radps);
  hash = append_legacy_vector(hash, state.accumulated_force_body_n);
  hash = append_legacy_vector(hash, state.accumulated_moment_body_nm);
  hash = append_legacy_double(hash, state.aircraft_mass_balance.total_mass_kg);
  hash = append_legacy_double(hash, state.aircraft_mass_balance.fuel_mass_kg);
  hash = append_legacy_double(hash, state.aircraft_mass_balance.payload_mass_kg);
  hash = append_legacy_vector(hash, state.aircraft_mass_balance.center_of_gravity_body_m);
  return append_legacy_inertia_tensor(hash, state.aircraft_mass_balance.inertia_tensor_kg_m2);
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

std::string join_errors(const std::vector<std::string>& errors) {
  std::string message;
  for (const std::string& error : errors) {
    if (!message.empty()) {
      message += "; ";
    }
    message += error;
  }
  return message;
}

std::vector<std::string> split_fields(std::string_view line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const std::size_t next = line.find('|', start);
    if (next == std::string_view::npos) {
      fields.emplace_back(line.substr(start));
      break;
    }
    fields.emplace_back(line.substr(start, next - start));
    start = next + 1;
  }
  return fields;
}

std::string join_fields(const std::vector<std::string>& fields) {
  std::string line;
  for (const std::string& field : fields) {
    if (!line.empty()) {
      line += '|';
    }
    line += field;
  }
  return line;
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
  require(loaded.loaded,
          ("saved telemetry file must load: " + join_errors(loaded.errors)).c_str());
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

void legacy_telemetry_files_still_load() {
  const std::filesystem::path telemetry_path =
      std::filesystem::temp_directory_path() / "flying-legacy-telemetry-load-test.flytelem";
  remove_temp_file(telemetry_path);

  const TelemetryRecording recording = make_recording();
  const auto saved = flying::core_sim::save_telemetry_file_atomic(telemetry_path, recording);
  require(saved.saved, "source telemetry file must save");

  std::string current_text = read_text(telemetry_path);
  std::string legacy_text;
  std::size_t start = 0;
  std::size_t legacy_frame_index = 0;
  while (start <= current_text.size()) {
    const std::size_t next = current_text.find('\n', start);
    const std::string_view line =
        next == std::string::npos
            ? std::string_view{current_text}.substr(start)
            : std::string_view{current_text}.substr(start, next - start);
    std::string legacy_line{line};
    if (!line.empty()) {
      std::vector<std::string> fields = split_fields(line);
      if (fields.front() == "initial_state") {
        require(fields.size() == 64, "current initial_state field count must match test fixture");
        std::vector<std::string> legacy_fields(fields.begin(), fields.begin() + 34);
        legacy_fields.push_back(std::to_string(hash_legacy_state(recording.initial_state)));
        legacy_line = join_fields(legacy_fields);
      } else if (fields.front() == "frame") {
        require(fields.size() == 94, "current frame field count must match test fixture");
        require(legacy_frame_index < recording.frames.size(),
                "legacy frame fixture must stay aligned with recording frames");
        std::vector<std::string> legacy_fields(fields.begin(), fields.begin() + 64);
        legacy_fields.push_back(
            std::to_string(hash_legacy_state(recording.frames[legacy_frame_index].state)));
        ++legacy_frame_index;
        legacy_line = join_fields(legacy_fields);
      }
    }
    legacy_text += legacy_line;
    if (next == std::string::npos) {
      break;
    }
    legacy_text += '\n';
    start = next + 1;
  }

  {
    std::ofstream output(telemetry_path, std::ios::trunc);
    output << legacy_text;
  }

  const auto loaded = flying::core_sim::load_telemetry_file(telemetry_path);
  require(loaded.loaded,
          ("legacy telemetry file must load: " + join_errors(loaded.errors)).c_str());
  require(loaded.recording.frames.size() == recording.frames.size(),
          "legacy telemetry frame count must load");
  require(loaded.recording.frames.front().state.aircraft_mass_balance.cg_within_envelope,
          "legacy telemetry should default missing CG envelope state");
  require(loaded.recording.frames.front().state.weather.source ==
              flying::core_sim::WeatherSource::Manual,
          "legacy telemetry should default missing weather source");
  require(loaded.recording.frames.front().state.weather.visibility_m == 30'000.0,
          "legacy telemetry should default missing visibility");
  require(loaded.recording.frames.front().state.weather.runway_friction_scale == 1.0,
          "legacy telemetry should default missing runway friction");
  require(loaded.recording.frames.front().state.weather.atmosphere.static_pressure_pa == 101'325.0,
          "legacy telemetry should default missing atmosphere");

  ReplayEnvironment environment = flying::core_sim::make_current_replay_environment();
  environment.rigid_body_parameters = loaded.recording.rigid_body_parameters;
  environment.data_packages.push_back({"pilot-region-fixtures", "v1"});
  CoreSimulator replay_simulator{loaded.recording.rigid_body_parameters};
  const auto replay = flying::core_sim::replay_recording(
      loaded.recording,
      replay_simulator,
      environment,
      ReplayCompatibilityPolicy::RefuseOnMismatch);
  require(replay.played, "legacy telemetry replay must play");
  require(replay.deterministic,
          "legacy telemetry replay must remain deterministic with defaulted weather fields");
  require(replay.replayed_frames == loaded.recording.frames.size(),
          "legacy telemetry replay must consume every frame");

  std::string tampered_text;
  bool tampered_frame = false;
  start = 0;
  while (start <= legacy_text.size()) {
    const std::size_t next = legacy_text.find('\n', start);
    const std::string_view line =
        next == std::string::npos
            ? std::string_view{legacy_text}.substr(start)
            : std::string_view{legacy_text}.substr(start, next - start);
    std::string tampered_line{line};
    if (!tampered_frame && !line.empty()) {
      std::vector<std::string> fields = split_fields(line);
      if (fields.front() == "frame") {
        require(fields.size() == 65, "legacy frame field count must match tamper fixture");
        fields[31] = std::to_string(std::stod(fields[31]) + 1.0);
        tampered_line = join_fields(fields);
        tampered_frame = true;
      }
    }
    tampered_text += tampered_line;
    if (next == std::string::npos) {
      break;
    }
    tampered_text += '\n';
    start = next + 1;
  }
  require(tampered_frame, "legacy telemetry tamper fixture must modify one frame");
  {
    std::ofstream output(telemetry_path, std::ios::trunc);
    output << tampered_text;
  }
  const auto tampered = flying::core_sim::load_telemetry_file(telemetry_path);
  require(!tampered.loaded, "tampered legacy telemetry must not load with a stale state hash");

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
  legacy_telemetry_files_still_load();
  exports_include_validation_metadata_and_flight_state();
  incompatible_versions_refuse_or_warn_before_replay();
  return 0;
}
