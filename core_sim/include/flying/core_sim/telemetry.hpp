#pragma once

#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/simulator.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace flying::core_sim {

inline constexpr std::string_view kTelemetrySchemaVersion = "flying.telemetry.v1";
inline constexpr std::string_view kTelemetryJsonExportSchemaVersion =
    "flying.telemetry-export.json.v1";
inline constexpr std::string_view kTelemetryCsvExportSchemaVersion =
    "flying.telemetry-export.csv.v1";

[[nodiscard]] std::string_view core_sim_version() noexcept;
[[nodiscard]] std::int64_t current_unix_time_ms() noexcept;

struct DataPackageVersion {
  std::string package_id;
  std::string version;
};

struct DeterminismTolerance {
  double simulation_time_s{1.0e-12};
  double position_m{1.0e-9};
  double velocity_mps{1.0e-12};
  double quaternion_component{1.0e-12};
  double angular_velocity_radps{1.0e-12};
  double force_n{1.0e-9};
  double moment_nm{1.0e-9};
};

struct EngineStateSample {
  bool engine_running{};
  double throttle_norm{};
  double mixture_norm{1.0};
  double propeller_norm{1.0};
};

struct TelemetryMetadata {
  std::string schema_version{std::string(kTelemetrySchemaVersion)};
  std::string core_sim_version{std::string(flying::core_sim::core_sim_version())};
  AircraftConfigurationIdentity aircraft{};
  std::string simulation_configuration_id;
  std::string input_profile_id;
  std::string scenario_location_id;
  std::string scenario_start_mode;
  std::string session_id;
  std::int64_t started_unix_ms{};
  double fixed_step_s{kFixedStepSeconds};
  DeterminismTolerance deterministic_tolerance{};
  std::vector<DataPackageVersion> data_packages;
};

struct TelemetryFrame {
  std::uint64_t frame_index{};
  std::int64_t host_time_unix_ms{};
  double caller_delta_s{};

  std::uint32_t steps_executed{};
  double fixed_step_s{};
  double consumed_time_s{};
  double remaining_accumulator_s{};
  std::uint64_t total_steps{};

  ControlInputSample core_input{};
  AircraftControlInputSample aircraft_controls{};
  EngineStateSample engine{};

  AuthoritativeState state{};
  std::uint64_t state_hash{};
};

struct TelemetryRecording {
  TelemetryMetadata metadata{};
  RigidBodyParameters rigid_body_parameters{};
  AuthoritativeState initial_state{};
  FlightDynamicsInitialCondition initial_flight_dynamics{};
  AircraftControlInputSample initial_aircraft_controls{};
  std::vector<TelemetryFrame> frames;
};

struct TelemetryWriteResult {
  bool saved{};
  std::vector<std::string> errors;
};

struct TelemetryLoadResult {
  bool loaded{};
  TelemetryRecording recording{};
  std::vector<std::string> errors;
};

struct TelemetryExportResult {
  bool exported{};
  std::vector<std::string> errors;
};

struct ReplayEnvironment {
  std::string telemetry_schema_version{std::string(kTelemetrySchemaVersion)};
  std::string core_sim_version{std::string(flying::core_sim::core_sim_version())};
  AircraftConfigurationIdentity aircraft{};
  RigidBodyParameters rigid_body_parameters{};
  std::vector<DataPackageVersion> data_packages;
};

enum class ReplayCompatibilityPolicy {
  RefuseOnMismatch,
  WarnOnMismatch,
};

struct ReplayCompatibilityResult {
  bool compatible{};
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
};

struct ReplayMismatch {
  std::uint64_t frame_index{};
  std::uint64_t expected_state_hash{};
  std::uint64_t actual_state_hash{};
  double max_state_error{};
  std::string reason;
};

struct ReplayResult {
  bool played{};
  bool deterministic{};
  bool refused{};
  std::uint64_t replayed_frames{};
  std::uint64_t final_state_hash{};
  std::vector<ReplayMismatch> mismatches;
  std::vector<std::string> warnings;
  std::vector<std::string> errors;
};

class TelemetryRecorder {
public:
  TelemetryRecorder(TelemetryMetadata metadata,
                    AuthoritativeState initial_state,
                    FlightDynamicsInitialCondition initial_flight_dynamics = {},
                    AircraftControlInputSample initial_aircraft_controls = {},
                    RigidBodyParameters rigid_body_parameters = {});

  [[nodiscard]] const TelemetryRecording& recording() const noexcept;
  [[nodiscard]] TelemetryRecording& recording() noexcept;

  const TelemetryFrame& record_advance(double caller_delta_s,
                                       const ControlInputSample& core_input,
                                       const AdvanceReport& report,
                                       const AuthoritativeState& state);
  const TelemetryFrame& record_advance(double caller_delta_s,
                                       const ControlInputSample& core_input,
                                       const AircraftControlInputSample& aircraft_controls,
                                       const EngineStateSample& engine,
                                       const AdvanceReport& report,
                                       const AuthoritativeState& state,
                                       std::int64_t host_time_unix_ms);

private:
  TelemetryRecording recording_;
};

[[nodiscard]] TelemetryMetadata make_default_telemetry_metadata();
[[nodiscard]] EngineStateSample make_engine_state_sample(
    const AircraftControlInputSample& controls,
    bool engine_running) noexcept;
[[nodiscard]] ReplayEnvironment make_current_replay_environment();

[[nodiscard]] std::vector<std::string> validate_telemetry_recording(
    const TelemetryRecording& recording);
[[nodiscard]] ReplayCompatibilityResult check_replay_compatibility(
    const TelemetryMetadata& metadata,
    const ReplayEnvironment& environment);
[[nodiscard]] ReplayResult replay_recording(
    const TelemetryRecording& recording,
    CoreSimulator& simulator,
    const ReplayEnvironment& environment = make_current_replay_environment(),
    ReplayCompatibilityPolicy compatibility_policy = ReplayCompatibilityPolicy::RefuseOnMismatch);

[[nodiscard]] TelemetryWriteResult save_telemetry_file_atomic(
    const std::filesystem::path& path,
    const TelemetryRecording& recording);
[[nodiscard]] TelemetryLoadResult load_telemetry_file(const std::filesystem::path& path);
[[nodiscard]] TelemetryExportResult export_telemetry_csv(
    const std::filesystem::path& path,
    const TelemetryRecording& recording);
[[nodiscard]] TelemetryExportResult export_telemetry_json(
    const std::filesystem::path& path,
    const TelemetryRecording& recording);

} // namespace flying::core_sim
