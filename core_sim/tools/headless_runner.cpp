#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/simulator.hpp"
#include "flying/core_sim/telemetry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>

namespace {

using flying::core_sim::AuthoritativeState;
using flying::core_sim::CallerFrameInput;
using flying::core_sim::ControlInputSample;
using flying::core_sim::CoreSimulator;
using flying::core_sim::RigidBodyParameters;

struct RunnerOptions {
  std::optional<std::filesystem::path> telemetry_output_path;
  std::optional<std::filesystem::path> replay_input_path;
  std::optional<std::filesystem::path> csv_export_path;
  std::optional<std::filesystem::path> json_export_path;
  bool help_requested{};
};

struct HeadlessRunResult {
  std::uint64_t total_steps{};
  std::uint64_t final_hash{};
  flying::core_sim::TelemetryRecording recording{};
};

void print_usage(const char* executable) {
  std::cout << "Usage: " << executable
            << " [--telemetry PATH] [--replay PATH] [--export-csv PATH] [--export-json PATH]\n";
}

std::optional<RunnerOptions> parse_options(int argc, char** argv) {
  RunnerOptions options{};
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    auto next_path = [&]() -> std::optional<std::filesystem::path> {
      if (index + 1 >= argc) {
        std::cerr << "missing path after " << argument << '\n';
        return std::nullopt;
      }
      ++index;
      return std::filesystem::path(argv[index]);
    };

    if (argument == "--help" || argument == "-h") {
      options.help_requested = true;
    } else if (argument == "--telemetry") {
      const auto path = next_path();
      if (!path) {
        return std::nullopt;
      }
      options.telemetry_output_path = *path;
    } else if (argument == "--replay") {
      const auto path = next_path();
      if (!path) {
        return std::nullopt;
      }
      options.replay_input_path = *path;
    } else if (argument == "--export-csv") {
      const auto path = next_path();
      if (!path) {
        return std::nullopt;
      }
      options.csv_export_path = *path;
    } else if (argument == "--export-json") {
      const auto path = next_path();
      if (!path) {
        return std::nullopt;
      }
      options.json_export_path = *path;
    } else {
      std::cerr << "unknown argument: " << argument << '\n';
      return std::nullopt;
    }
  }

  return options;
}

AuthoritativeState make_initial_state() {
  AuthoritativeState initial_state{};
  initial_state.ecef_position_m = {3'980'000.0, 1'100'000.0, 4'870'000.0};
  initial_state.ecef_velocity_mps = {32.0, 0.0, -0.2};
  return initial_state;
}

std::array<CallerFrameInput, 8> make_input_stream() {
  return {{
    {1.0 / 480.0, ControlInputSample{{1'100.0, 0.0, 80.0}, {0.0, 18.0, 0.0}}},
    {1.0 / 480.0, ControlInputSample{{1'100.0, 0.0, 80.0}, {0.0, 18.0, 0.0}}},
    {1.0 / 120.0, ControlInputSample{{1'250.0, 10.0, 75.0}, {2.0, 16.0, -4.0}}},
    {1.0 / 240.0, ControlInputSample{{1'300.0, -12.0, 70.0}, {-3.0, 14.0, -6.0}}},
    {1.0 / 80.0, ControlInputSample{{1'420.0, -18.0, 65.0}, {-6.0, 12.0, -8.0}}},
    {0.001, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
    {0.004, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
    {0.002, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
  }};
}

HeadlessRunResult run_scripted_session(bool capture_telemetry) {
  const RigidBodyParameters parameters{
    975.0,
    {875.0, 1'060.0, 1'420.0},
  };

  CoreSimulator simulator{parameters};
  simulator.reset(make_initial_state());

  auto metadata = flying::core_sim::make_default_telemetry_metadata();
  metadata.session_id = "headless-core-sim";
  metadata.simulation_configuration_id = "headless.synthetic-rigid-body.v1";
  metadata.input_profile_id = "headless.scripted-force-moment.v1";

  flying::core_sim::AircraftControlInputSample initial_controls{};
  initial_controls.throttle_norm = 0.72;
  initial_controls.mixture_norm = 1.0;
  initial_controls.propeller_norm = 1.0;
  flying::core_sim::TelemetryRecorder recorder{
    metadata,
    simulator.state(),
    {},
    initial_controls,
    parameters};

  std::cout << std::setprecision(17);
  std::cout << "fixed_step_s=" << simulator.fixed_step_s() << '\n';

  std::uint64_t total_steps = 0;
  const auto input_stream = make_input_stream();
  for (std::size_t frame_index = 0; frame_index < input_stream.size(); ++frame_index) {
    const auto report =
        simulator.advance(input_stream[frame_index].elapsed_time_s, input_stream[frame_index].controls);
    total_steps = report.total_steps;
    if (capture_telemetry) {
      recorder.record_advance(input_stream[frame_index].elapsed_time_s,
                              input_stream[frame_index].controls,
                              report,
                              simulator.state());
    }
    std::cout << "frame=" << frame_index
              << " caller_delta_s=" << input_stream[frame_index].elapsed_time_s
              << " steps=" << report.steps_executed
              << " remaining_accumulator_s=" << report.remaining_accumulator_s << '\n';
  }

  const auto final_hash = flying::core_sim::hash_state(simulator.state());
  std::cout << "total_steps=" << total_steps << '\n';
  std::cout << "state_hash=0x" << std::hex << std::setw(16) << std::setfill('0') << final_hash
            << std::dec << '\n';

  return {total_steps, final_hash, recorder.recording()};
}

bool export_recording(const flying::core_sim::TelemetryRecording& recording,
                      const RunnerOptions& options) {
  if (options.csv_export_path) {
    const auto exported =
        flying::core_sim::export_telemetry_csv(*options.csv_export_path, recording);
    if (!exported.exported) {
      for (const std::string& error : exported.errors) {
        std::cerr << "CSV export failed: " << error << '\n';
      }
      return false;
    }
    std::cout << "csv_export=" << options.csv_export_path->string() << '\n';
  }

  if (options.json_export_path) {
    const auto exported =
        flying::core_sim::export_telemetry_json(*options.json_export_path, recording);
    if (!exported.exported) {
      for (const std::string& error : exported.errors) {
        std::cerr << "JSON export failed: " << error << '\n';
      }
      return false;
    }
    std::cout << "json_export=" << options.json_export_path->string() << '\n';
  }

  return true;
}

int replay_recording(const RunnerOptions& options) {
  const auto loaded = flying::core_sim::load_telemetry_file(*options.replay_input_path);
  if (!loaded.loaded) {
    for (const std::string& error : loaded.errors) {
      std::cerr << "telemetry load failed: " << error << '\n';
    }
    return 2;
  }

  flying::core_sim::ReplayEnvironment environment =
      flying::core_sim::make_current_replay_environment();
  environment.rigid_body_parameters = loaded.recording.rigid_body_parameters;
  CoreSimulator simulator{loaded.recording.rigid_body_parameters};
  const auto replay = flying::core_sim::replay_recording(
      loaded.recording,
      simulator,
      environment,
      flying::core_sim::ReplayCompatibilityPolicy::RefuseOnMismatch);
  for (const std::string& warning : replay.warnings) {
    std::cerr << "replay warning: " << warning << '\n';
  }
  if (replay.refused || !replay.errors.empty()) {
    for (const std::string& error : replay.errors) {
      std::cerr << "replay refused: " << error << '\n';
    }
    return 2;
  }
  if (!replay.deterministic) {
    for (const auto& mismatch : replay.mismatches) {
      std::cerr << "replay mismatch frame=" << mismatch.frame_index
                << " expected_hash=" << mismatch.expected_state_hash
                << " actual_hash=" << mismatch.actual_state_hash
                << " max_state_error=" << mismatch.max_state_error
                << " reason=" << mismatch.reason << '\n';
    }
    return 3;
  }

  std::cout << "replay_frames=" << replay.replayed_frames << '\n';
  std::cout << "replay_state_hash=0x" << std::hex << std::setw(16) << std::setfill('0')
            << replay.final_state_hash << std::dec << '\n';
  std::cout << "replay_deterministic=1\n";
  return export_recording(loaded.recording, options) ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) {
  const auto options = parse_options(argc, argv);
  if (!options) {
    print_usage(argv[0]);
    return 2;
  }
  if (options->help_requested) {
    print_usage(argv[0]);
    return 0;
  }
  if (options->replay_input_path) {
    return replay_recording(*options);
  }

  const bool capture_telemetry =
      options->telemetry_output_path.has_value() ||
      options->csv_export_path.has_value() ||
      options->json_export_path.has_value();
  const HeadlessRunResult result = run_scripted_session(capture_telemetry);
  (void)result.total_steps;
  (void)result.final_hash;

  if (options->telemetry_output_path) {
    const auto saved =
        flying::core_sim::save_telemetry_file_atomic(*options->telemetry_output_path, result.recording);
    if (!saved.saved) {
      for (const std::string& error : saved.errors) {
        std::cerr << "telemetry save failed: " << error << '\n';
      }
      return 2;
    }
    std::cout << "telemetry=" << options->telemetry_output_path->string() << '\n';
  }

  return export_recording(result.recording, *options) ? 0 : 2;
}
