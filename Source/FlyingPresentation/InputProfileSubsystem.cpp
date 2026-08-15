#include "InputProfileSubsystem.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace flying::presentation {

namespace {

[[nodiscard]] std::filesystem::path backup_path_for(const std::filesystem::path& path) {
  std::filesystem::path backup = path;
  backup += ".bak";
  return backup;
}

[[nodiscard]] std::string json_escape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
    case '\\':
      escaped += "\\\\";
      break;
    case '"':
      escaped += "\\\"";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\r";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped += character;
      break;
    }
  }
  return escaped;
}

void append_source_json(std::ostream& output, const core_sim::PhysicalInputBinding& source) {
  output << "{\"deviceClass\":\"" << core_sim::to_string(source.device_class)
         << "\",\"deviceId\":\"" << json_escape(source.device_id)
         << "\",\"controlPath\":\"" << json_escape(source.control_path) << "\"}";
}

} // namespace

[[nodiscard]] InputProfileSettings make_default_input_profile_settings() {
  return core_sim::make_default_input_settings();
}

[[nodiscard]] std::vector<std::string> validate_input_profile_settings(
    const InputProfileSettings& settings) {
  return core_sim::validate_input_settings(settings);
}

[[nodiscard]] core_sim::MappedInputState map_input_profile_frame(
    const InputProfileSettings& settings,
    const core_sim::RawInputFrame& frame) {
  return core_sim::map_input_frame(settings, frame);
}

[[nodiscard]] double apply_input_profile_axis_response(
    double raw_value,
    const core_sim::AxisCalibration& calibration) noexcept {
  return core_sim::apply_axis_calibration(raw_value, calibration);
}

[[nodiscard]] std::string serialize_input_profiles_schema_json(
    const InputProfileSettings& settings) {
  std::ostringstream output;
  output << "{\"schemaVersion\":\"" << json_escape(settings.schema_version)
         << "\",\"activeProfileId\":\"" << json_escape(settings.active_profile_id)
         << "\",\"profiles\":[";

  for (std::size_t profile_index = 0; profile_index < settings.profiles.size(); ++profile_index) {
    const core_sim::InputDeviceProfile& profile = settings.profiles[profile_index];
    if (profile_index > 0) {
      output << ',';
    }
    output << "{\"profileId\":\"" << json_escape(profile.profile_id)
           << "\",\"displayName\":\"" << json_escape(profile.display_name)
           << "\",\"deviceClass\":\"" << core_sim::to_string(profile.device_class)
           << "\",\"hardwareId\":\"" << json_escape(profile.hardware_id)
           << "\",\"axisBindings\":[";

    for (std::size_t binding_index = 0; binding_index < profile.axis_bindings.size(); ++binding_index) {
      const core_sim::AxisBinding& binding = profile.axis_bindings[binding_index];
      if (binding_index > 0) {
        output << ',';
      }
      output << "{\"axis\":\"" << core_sim::to_string(binding.axis) << "\",\"source\":";
      append_source_json(output, binding.source);
      output << ",\"calibration\":{\"deadZoneNorm\":" << binding.calibration.dead_zone_norm
             << ",\"responseCurve\":" << binding.calibration.response_curve
             << ",\"inverted\":" << (binding.calibration.inverted ? "true" : "false")
             << ",\"saturationNegativeNorm\":" << binding.calibration.saturation_negative_norm
             << ",\"saturationPositiveNorm\":" << binding.calibration.saturation_positive_norm
             << "},\"scale\":" << binding.scale << "}";
    }

    output << "],\"commandBindings\":[";
    for (std::size_t binding_index = 0; binding_index < profile.command_bindings.size(); ++binding_index) {
      const core_sim::CommandBinding& binding = profile.command_bindings[binding_index];
      if (binding_index > 0) {
        output << ',';
      }
      output << "{\"command\":\"" << core_sim::to_string(binding.command) << "\",\"source\":";
      append_source_json(output, binding.source);
      output << ",\"activationThreshold\":" << binding.activation_threshold << "}";
    }
    output << "]}";
  }

  output << "]}";
  return output.str();
}

[[nodiscard]] InputProfileLoadResult load_input_profiles(
    const std::filesystem::path& path) {
  InputProfileLoadResult primary = core_sim::load_input_settings(path);
  if (primary.loaded) {
    return primary;
  }

  InputProfileLoadResult backup = core_sim::load_input_settings(backup_path_for(path));
  if (backup.loaded) {
    return backup;
  }

  primary.errors.insert(primary.errors.end(), backup.errors.begin(), backup.errors.end());
  return primary;
}

[[nodiscard]] InputProfileWriteResult save_input_profiles_atomic(
    const std::filesystem::path& path,
    const InputProfileSettings& settings) {
  InputProfileWriteResult result = core_sim::save_input_settings_atomic(path, settings);
  if (!result.saved) {
    return result;
  }

  InputProfileWriteResult backup = core_sim::save_input_settings_atomic(backup_path_for(path), settings);
  if (!backup.saved) {
    result.saved = false;
    result.errors.insert(result.errors.end(), backup.errors.begin(), backup.errors.end());
  }
  return result;
}

} // namespace flying::presentation
