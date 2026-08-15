#pragma once

#include "flying/core_sim/telemetry.hpp"

#include <filesystem>

namespace flying::telemetry_replay {

using flying::core_sim::DataPackageVersion;
using flying::core_sim::DeterminismTolerance;
using flying::core_sim::EngineStateSample;
using flying::core_sim::ReplayCompatibilityPolicy;
using flying::core_sim::ReplayCompatibilityResult;
using flying::core_sim::ReplayEnvironment;
using flying::core_sim::ReplayMismatch;
using flying::core_sim::ReplayResult;
using flying::core_sim::TelemetryExportResult;
using flying::core_sim::TelemetryFrame;
using flying::core_sim::TelemetryLoadResult;
using flying::core_sim::TelemetryMetadata;
using flying::core_sim::TelemetryRecorder;
using flying::core_sim::TelemetryRecording;
using flying::core_sim::TelemetryWriteResult;

using flying::core_sim::check_replay_compatibility;
using flying::core_sim::core_sim_version;
using flying::core_sim::current_unix_time_ms;
using flying::core_sim::export_telemetry_csv;
using flying::core_sim::export_telemetry_json;
using flying::core_sim::kTelemetryCsvExportSchemaVersion;
using flying::core_sim::kTelemetryJsonExportSchemaVersion;
using flying::core_sim::kTelemetrySchemaVersion;
using flying::core_sim::load_telemetry_file;
using flying::core_sim::make_current_replay_environment;
using flying::core_sim::make_default_telemetry_metadata;
using flying::core_sim::make_engine_state_sample;
using flying::core_sim::replay_recording;
using flying::core_sim::save_telemetry_file_atomic;
using flying::core_sim::validate_telemetry_recording;

class ReplayPlayer {
public:
  [[nodiscard]] ReplayResult play(
      const TelemetryRecording& recording,
      flying::core_sim::CoreSimulator& simulator,
      const ReplayEnvironment& environment = make_current_replay_environment(),
      ReplayCompatibilityPolicy compatibility_policy =
          ReplayCompatibilityPolicy::RefuseOnMismatch) const {
    return replay_recording(recording, simulator, environment, compatibility_policy);
  }
};

class TelemetryExporter {
public:
  [[nodiscard]] TelemetryExportResult export_csv(
      const std::filesystem::path& path,
      const TelemetryRecording& recording) const {
    return export_telemetry_csv(path, recording);
  }

  [[nodiscard]] TelemetryExportResult export_json(
      const std::filesystem::path& path,
      const TelemetryRecording& recording) const {
    return export_telemetry_json(path, recording);
  }
};

} // namespace flying::telemetry_replay
