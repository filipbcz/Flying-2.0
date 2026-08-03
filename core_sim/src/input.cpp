#include "flying/core_sim/input.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace flying::core_sim {
namespace {

constexpr std::string_view kInputSettingsSchemaVersion = "flying.input-settings.v1";
std::atomic<std::uint64_t> g_temp_file_sequence{0};

[[nodiscard]] bool is_finite(double value) noexcept {
  return std::isfinite(value);
}

[[nodiscard]] double clamp(double value, double minimum, double maximum) noexcept {
  return std::clamp(value, minimum, maximum);
}

[[nodiscard]] bool contains_record_delimiter(std::string_view value) noexcept {
  return value.find('|') != std::string_view::npos ||
         value.find('\n') != std::string_view::npos ||
         value.find('\r') != std::string_view::npos;
}

[[nodiscard]] std::string record_delimiter_error_suffix() {
  return " must not contain '|', newline, or carriage return";
}

[[nodiscard]] bool is_unit_axis(FlightControlAxis axis) noexcept {
  switch (axis) {
  case FlightControlAxis::Throttle:
  case FlightControlAxis::Mixture:
  case FlightControlAxis::Propeller:
  case FlightControlAxis::BrakeLeft:
  case FlightControlAxis::BrakeRight:
  case FlightControlAxis::BrakeCombined:
    return true;
  case FlightControlAxis::Pitch:
  case FlightControlAxis::Roll:
  case FlightControlAxis::Yaw:
  case FlightControlAxis::ElevatorTrim:
  case FlightControlAxis::AileronTrim:
  case FlightControlAxis::RudderTrim:
  case FlightControlAxis::ViewPanX:
  case FlightControlAxis::ViewPanY:
  case FlightControlAxis::ViewZoom:
    return false;
  }

  return false;
}

[[nodiscard]] std::vector<std::string_view> split_fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const std::size_t next = line.find('|', start);
    if (next == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }

    fields.push_back(line.substr(start, next - start));
    start = next + 1;
  }

  return fields;
}

[[nodiscard]] std::string to_owned(std::string_view value) {
  return std::string(value.begin(), value.end());
}

[[nodiscard]] bool parse_bool(std::string_view value, bool& parsed) noexcept {
  if (value == "0") {
    parsed = false;
    return true;
  }
  if (value == "1") {
    parsed = true;
    return true;
  }
  return false;
}

[[nodiscard]] bool parse_double(std::string_view value, double& parsed) noexcept {
  try {
    std::size_t consumed = 0;
    const std::string owned = to_owned(value);
    parsed = std::stod(owned, &consumed);
    return consumed == owned.size() && std::isfinite(parsed);
  } catch (const std::exception&) {
    return false;
  }
}

[[nodiscard]] std::string double_to_string(double value) {
  std::ostringstream stream;
  stream.precision(std::numeric_limits<double>::max_digits10);
  stream << value;
  return stream.str();
}

[[nodiscard]] const InputDeviceProfile* find_active_profile(const InputSettings& settings) noexcept {
  if (!settings.active_profile_id.empty()) {
    const auto active = std::find_if(
        settings.profiles.begin(),
        settings.profiles.end(),
        [&](const InputDeviceProfile& profile) {
          return profile.profile_id == settings.active_profile_id;
        });
    if (active != settings.profiles.end()) {
      return &*active;
    }
  }

  return settings.profiles.empty() ? nullptr : &settings.profiles.front();
}

[[nodiscard]] bool binding_matches(const PhysicalInputBinding& binding,
                                   const PhysicalInputBinding& source) noexcept {
  return binding.device_class == source.device_class &&
         binding.control_path == source.control_path &&
         (binding.device_id.empty() || binding.device_id == source.device_id);
}

void apply_axis_value(FlightControlAxis axis, double value, MappedInputState& mapped) {
  const double signed_value = clamp(value, -1.0, 1.0);
  const double unit_value = clamp(value, 0.0, 1.0);

  switch (axis) {
  case FlightControlAxis::Pitch:
    mapped.aircraft.elevator_norm = signed_value;
    break;
  case FlightControlAxis::Roll:
    mapped.aircraft.aileron_norm = signed_value;
    break;
  case FlightControlAxis::Yaw:
    mapped.aircraft.rudder_norm = signed_value;
    break;
  case FlightControlAxis::Throttle:
    mapped.aircraft.throttle_norm = unit_value;
    break;
  case FlightControlAxis::Mixture:
    mapped.aircraft.mixture_norm = unit_value;
    break;
  case FlightControlAxis::Propeller:
    mapped.aircraft.propeller_norm = unit_value;
    break;
  case FlightControlAxis::BrakeLeft:
    mapped.aircraft.brake_left_norm = unit_value;
    break;
  case FlightControlAxis::BrakeRight:
    mapped.aircraft.brake_right_norm = unit_value;
    break;
  case FlightControlAxis::BrakeCombined:
    mapped.aircraft.brake_left_norm = unit_value;
    mapped.aircraft.brake_right_norm = unit_value;
    break;
  case FlightControlAxis::ElevatorTrim:
    mapped.aircraft.elevator_trim_norm = signed_value;
    break;
  case FlightControlAxis::AileronTrim:
    mapped.aircraft.aileron_trim_norm = signed_value;
    break;
  case FlightControlAxis::RudderTrim:
    mapped.aircraft.rudder_trim_norm = signed_value;
    break;
  case FlightControlAxis::ViewPanX:
    mapped.view_pan_x_norm = signed_value;
    break;
  case FlightControlAxis::ViewPanY:
    mapped.view_pan_y_norm = signed_value;
    break;
  case FlightControlAxis::ViewZoom:
    mapped.view_zoom_norm = signed_value;
    break;
  }
}

void add_calibration_error(const AxisCalibration& calibration,
                           std::string_view context,
                           std::vector<std::string>& errors) {
  if (!is_finite(calibration.dead_zone_norm) ||
      calibration.dead_zone_norm < 0.0 ||
      calibration.dead_zone_norm >= 1.0) {
    errors.push_back(to_owned(context) + " dead_zone_norm must be finite and in [0, 1)");
  }
  if (!is_finite(calibration.response_curve) ||
      calibration.response_curve < 0.1 ||
      calibration.response_curve > 8.0) {
    errors.push_back(to_owned(context) + " response_curve must be finite and in [0.1, 8]");
  }
  if (!is_finite(calibration.saturation_negative_norm) ||
      calibration.saturation_negative_norm <= calibration.dead_zone_norm ||
      calibration.saturation_negative_norm > 1.0) {
    errors.push_back(
        to_owned(context) +
        " saturation_negative_norm must be finite, <= 1, and greater than the dead zone");
  }
  if (!is_finite(calibration.saturation_positive_norm) ||
      calibration.saturation_positive_norm <= calibration.dead_zone_norm ||
      calibration.saturation_positive_norm > 1.0) {
    errors.push_back(
        to_owned(context) +
        " saturation_positive_norm must be finite, <= 1, and greater than the dead zone");
  }
}

void append_profile(std::ostream& output, const InputDeviceProfile& profile) {
  output << "profile|"
         << profile.profile_id << '|'
         << profile.display_name << '|'
         << to_string(profile.device_class) << '|'
         << profile.hardware_id << '\n';

  for (const AxisBinding& binding : profile.axis_bindings) {
    output << "axis|"
           << profile.profile_id << '|'
           << to_string(binding.axis) << '|'
           << to_string(binding.source.device_class) << '|'
           << binding.source.device_id << '|'
           << binding.source.control_path << '|'
           << double_to_string(binding.scale) << '|'
           << double_to_string(binding.calibration.dead_zone_norm) << '|'
           << double_to_string(binding.calibration.response_curve) << '|'
           << (binding.calibration.inverted ? "1" : "0") << '|'
           << double_to_string(binding.calibration.saturation_negative_norm) << '|'
           << double_to_string(binding.calibration.saturation_positive_norm) << '\n';
  }

  for (const CommandBinding& binding : profile.command_bindings) {
    output << "command|"
           << profile.profile_id << '|'
           << to_string(binding.command) << '|'
           << to_string(binding.source.device_class) << '|'
           << binding.source.device_id << '|'
           << binding.source.control_path << '|'
           << double_to_string(binding.activation_threshold) << '\n';
  }
}

[[nodiscard]] InputDeviceProfile* find_profile(InputSettings& settings, std::string_view profile_id) noexcept {
  const auto found = std::find_if(
      settings.profiles.begin(),
      settings.profiles.end(),
      [&](const InputDeviceProfile& profile) {
        return profile.profile_id == profile_id;
      });
  return found == settings.profiles.end() ? nullptr : &*found;
}

[[nodiscard]] bool replace_existing_target_axis(InputDeviceProfile& profile,
                                                const AxisBinding& binding) {
  const auto existing = std::find_if(
      profile.axis_bindings.begin(),
      profile.axis_bindings.end(),
      [&](const AxisBinding& candidate) {
        return candidate.axis == binding.axis &&
               candidate.source.device_class == binding.source.device_class &&
               candidate.source.device_id == binding.source.device_id &&
               candidate.source.control_path == binding.source.control_path;
      });
  if (existing == profile.axis_bindings.end()) {
    return false;
  }

  *existing = binding;
  return true;
}

[[nodiscard]] bool replace_existing_command(InputDeviceProfile& profile,
                                            const CommandBinding& binding) {
  const auto existing = std::find_if(
      profile.command_bindings.begin(),
      profile.command_bindings.end(),
      [&](const CommandBinding& candidate) {
        return candidate.command == binding.command &&
               candidate.source.device_class == binding.source.device_class &&
               candidate.source.device_id == binding.source.device_id &&
               candidate.source.control_path == binding.source.control_path;
      });
  if (existing == profile.command_bindings.end()) {
    return false;
  }

  *existing = binding;
  return true;
}

[[nodiscard]] std::uint64_t current_process_id() noexcept {
#ifdef _WIN32
  return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(::getpid());
#endif
}

[[nodiscard]] std::filesystem::path temp_path_for(const std::filesystem::path& path) {
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::uint64_t sequence = g_temp_file_sequence.fetch_add(1, std::memory_order_relaxed);
  std::filesystem::path temp = path;
  temp += ".tmp.";
  temp += std::to_string(current_process_id());
  temp += ".";
  temp += std::to_string(now);
  temp += ".";
  temp += std::to_string(sequence);
  return temp;
}

void remove_temp_file(const std::filesystem::path& temp) noexcept {
  if (temp.empty()) {
    return;
  }

  std::error_code remove_error;
  std::filesystem::remove(temp, remove_error);
}

[[nodiscard]] bool replace_file_atomically(const std::filesystem::path& temp,
                                           const std::filesystem::path& target,
                                           std::error_code& error) noexcept {
#ifdef _WIN32
  if (!MoveFileExW(temp.c_str(),
                   target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
  }
  error.clear();
  return true;
#else
  std::filesystem::rename(temp, target, error);
  return !error;
#endif
}

} // namespace

std::string_view to_string(InputDeviceClass value) noexcept {
  switch (value) {
  case InputDeviceClass::Keyboard:
    return "keyboard";
  case InputDeviceClass::Mouse:
    return "mouse";
  case InputDeviceClass::Gamepad:
    return "gamepad";
  case InputDeviceClass::HidFlightControl:
    return "hid_flight_control";
  }

  return "unknown";
}

std::string_view to_string(FlightControlAxis value) noexcept {
  switch (value) {
  case FlightControlAxis::Pitch:
    return "pitch";
  case FlightControlAxis::Roll:
    return "roll";
  case FlightControlAxis::Yaw:
    return "yaw";
  case FlightControlAxis::Throttle:
    return "throttle";
  case FlightControlAxis::Mixture:
    return "mixture";
  case FlightControlAxis::Propeller:
    return "propeller";
  case FlightControlAxis::BrakeLeft:
    return "brake_left";
  case FlightControlAxis::BrakeRight:
    return "brake_right";
  case FlightControlAxis::BrakeCombined:
    return "brake_combined";
  case FlightControlAxis::ElevatorTrim:
    return "elevator_trim";
  case FlightControlAxis::AileronTrim:
    return "aileron_trim";
  case FlightControlAxis::RudderTrim:
    return "rudder_trim";
  case FlightControlAxis::ViewPanX:
    return "view_pan_x";
  case FlightControlAxis::ViewPanY:
    return "view_pan_y";
  case FlightControlAxis::ViewZoom:
    return "view_zoom";
  }

  return "unknown";
}

std::string_view to_string(FlightCommand value) noexcept {
  switch (value) {
  case FlightCommand::ParkingBrakeToggle:
    return "parking_brake_toggle";
  case FlightCommand::BrakesHold:
    return "brakes_hold";
  case FlightCommand::FlapsUp:
    return "flaps_up";
  case FlightCommand::FlapsDown:
    return "flaps_down";
  case FlightCommand::GearToggle:
    return "gear_toggle";
  case FlightCommand::StarterToggle:
    return "starter_toggle";
  case FlightCommand::MasterBatteryToggle:
    return "master_battery_toggle";
  case FlightCommand::AvionicsToggle:
    return "avionics_toggle";
  case FlightCommand::MixtureCutoff:
    return "mixture_cutoff";
  case FlightCommand::MixtureRich:
    return "mixture_rich";
  case FlightCommand::PropellerFeatherToggle:
    return "propeller_feather_toggle";
  case FlightCommand::TrimReset:
    return "trim_reset";
  case FlightCommand::ViewReset:
    return "view_reset";
  case FlightCommand::PauseToggle:
    return "pause_toggle";
  }

  return "unknown";
}

std::optional<InputDeviceClass> input_device_class_from_string(std::string_view value) noexcept {
  if (value == "keyboard") {
    return InputDeviceClass::Keyboard;
  }
  if (value == "mouse") {
    return InputDeviceClass::Mouse;
  }
  if (value == "gamepad") {
    return InputDeviceClass::Gamepad;
  }
  if (value == "hid_flight_control") {
    return InputDeviceClass::HidFlightControl;
  }
  return std::nullopt;
}

std::optional<FlightControlAxis> flight_control_axis_from_string(std::string_view value) noexcept {
  if (value == "pitch") {
    return FlightControlAxis::Pitch;
  }
  if (value == "roll") {
    return FlightControlAxis::Roll;
  }
  if (value == "yaw") {
    return FlightControlAxis::Yaw;
  }
  if (value == "throttle") {
    return FlightControlAxis::Throttle;
  }
  if (value == "mixture") {
    return FlightControlAxis::Mixture;
  }
  if (value == "propeller") {
    return FlightControlAxis::Propeller;
  }
  if (value == "brake_left") {
    return FlightControlAxis::BrakeLeft;
  }
  if (value == "brake_right") {
    return FlightControlAxis::BrakeRight;
  }
  if (value == "brake_combined") {
    return FlightControlAxis::BrakeCombined;
  }
  if (value == "elevator_trim") {
    return FlightControlAxis::ElevatorTrim;
  }
  if (value == "aileron_trim") {
    return FlightControlAxis::AileronTrim;
  }
  if (value == "rudder_trim") {
    return FlightControlAxis::RudderTrim;
  }
  if (value == "view_pan_x") {
    return FlightControlAxis::ViewPanX;
  }
  if (value == "view_pan_y") {
    return FlightControlAxis::ViewPanY;
  }
  if (value == "view_zoom") {
    return FlightControlAxis::ViewZoom;
  }
  return std::nullopt;
}

std::optional<FlightCommand> flight_command_from_string(std::string_view value) noexcept {
  if (value == "parking_brake_toggle") {
    return FlightCommand::ParkingBrakeToggle;
  }
  if (value == "brakes_hold") {
    return FlightCommand::BrakesHold;
  }
  if (value == "flaps_up") {
    return FlightCommand::FlapsUp;
  }
  if (value == "flaps_down") {
    return FlightCommand::FlapsDown;
  }
  if (value == "gear_toggle") {
    return FlightCommand::GearToggle;
  }
  if (value == "starter_toggle") {
    return FlightCommand::StarterToggle;
  }
  if (value == "master_battery_toggle") {
    return FlightCommand::MasterBatteryToggle;
  }
  if (value == "avionics_toggle") {
    return FlightCommand::AvionicsToggle;
  }
  if (value == "mixture_cutoff") {
    return FlightCommand::MixtureCutoff;
  }
  if (value == "mixture_rich") {
    return FlightCommand::MixtureRich;
  }
  if (value == "propeller_feather_toggle") {
    return FlightCommand::PropellerFeatherToggle;
  }
  if (value == "trim_reset") {
    return FlightCommand::TrimReset;
  }
  if (value == "view_reset") {
    return FlightCommand::ViewReset;
  }
  if (value == "pause_toggle") {
    return FlightCommand::PauseToggle;
  }
  return std::nullopt;
}

std::vector<FlightControlAxis> required_bindable_axes() {
  return {
    FlightControlAxis::Pitch,
    FlightControlAxis::Roll,
    FlightControlAxis::Yaw,
    FlightControlAxis::Throttle,
    FlightControlAxis::Mixture,
    FlightControlAxis::Propeller,
    FlightControlAxis::BrakeCombined,
    FlightControlAxis::ElevatorTrim,
    FlightControlAxis::AileronTrim,
    FlightControlAxis::RudderTrim,
    FlightControlAxis::ViewPanX,
    FlightControlAxis::ViewPanY,
    FlightControlAxis::ViewZoom,
  };
}

std::vector<FlightCommand> core_cockpit_commands() {
  return {
    FlightCommand::ParkingBrakeToggle,
    FlightCommand::BrakesHold,
    FlightCommand::FlapsUp,
    FlightCommand::FlapsDown,
    FlightCommand::GearToggle,
    FlightCommand::StarterToggle,
    FlightCommand::MasterBatteryToggle,
    FlightCommand::AvionicsToggle,
    FlightCommand::MixtureCutoff,
    FlightCommand::MixtureRich,
    FlightCommand::PropellerFeatherToggle,
    FlightCommand::TrimReset,
    FlightCommand::ViewReset,
    FlightCommand::PauseToggle,
  };
}

double apply_axis_calibration(double raw_value, const AxisCalibration& calibration) noexcept {
  if (!std::isfinite(raw_value)) {
    return 0.0;
  }

  double value = clamp(raw_value, -1.0, 1.0);
  if (calibration.inverted) {
    value = -value;
  }

  const double sign = value < 0.0 ? -1.0 : 1.0;
  const double magnitude = std::abs(value);
  const double dead_zone = clamp(calibration.dead_zone_norm, 0.0, 0.999999);
  if (magnitude <= dead_zone) {
    return 0.0;
  }

  const double saturation =
      sign < 0.0
          ? clamp(calibration.saturation_negative_norm, dead_zone + 1.0e-9, 1.0)
          : clamp(calibration.saturation_positive_norm, dead_zone + 1.0e-9, 1.0);
  const double normalized = clamp((magnitude - dead_zone) / (saturation - dead_zone), 0.0, 1.0);
  const double curve = is_finite(calibration.response_curve) && calibration.response_curve > 0.0
                           ? calibration.response_curve
                           : 1.0;
  return sign * std::pow(normalized, curve);
}

MappedInputState map_input_frame(const InputSettings& settings, const RawInputFrame& frame) {
  const InputDeviceProfile* profile = find_active_profile(settings);
  return profile ? map_input_frame(*profile, frame) : MappedInputState{};
}

MappedInputState map_input_frame(const InputDeviceProfile& profile, const RawInputFrame& frame) {
  MappedInputState mapped{};

  for (const RawInputControlValue& raw : frame.controls) {
    if (!is_finite(raw.value)) {
      continue;
    }

    for (const AxisBinding& binding : profile.axis_bindings) {
      if (!binding_matches(binding.source, raw.source)) {
        continue;
      }

      const double calibrated = apply_axis_calibration(raw.value, binding.calibration);
      const double contribution = calibrated * binding.scale;
      apply_axis_value(
          binding.axis,
          is_unit_axis(binding.axis) ? clamp(contribution, 0.0, 1.0) : clamp(contribution, -1.0, 1.0),
          mapped);
    }

    for (const CommandBinding& binding : profile.command_bindings) {
      if (binding_matches(binding.source, raw.source) &&
          raw.value >= binding.activation_threshold) {
        mapped.commands.push_back({binding.command, raw.source});
      }
    }
  }

  return mapped;
}

InputSettings make_default_input_settings() {
  InputSettings settings{};
  settings.active_profile_id = "keyboard-mouse-default";

  InputDeviceProfile keyboard_mouse{};
  keyboard_mouse.profile_id = "keyboard-mouse-default";
  keyboard_mouse.display_name = "Keyboard and Mouse";
  keyboard_mouse.device_class = InputDeviceClass::Keyboard;
  keyboard_mouse.axis_bindings = {
    {FlightControlAxis::Pitch, {InputDeviceClass::Keyboard, {}, "KeyS"}, {}, 1.0},
    {FlightControlAxis::Pitch, {InputDeviceClass::Keyboard, {}, "KeyW"}, {}, -1.0},
    {FlightControlAxis::Roll, {InputDeviceClass::Keyboard, {}, "KeyD"}, {}, 1.0},
    {FlightControlAxis::Roll, {InputDeviceClass::Keyboard, {}, "KeyA"}, {}, -1.0},
    {FlightControlAxis::Yaw, {InputDeviceClass::Keyboard, {}, "KeyE"}, {}, 1.0},
    {FlightControlAxis::Yaw, {InputDeviceClass::Keyboard, {}, "KeyQ"}, {}, -1.0},
    {FlightControlAxis::Throttle, {InputDeviceClass::Keyboard, {}, "KeyLeftShift"}, {}, 1.0},
    {FlightControlAxis::BrakeCombined, {InputDeviceClass::Keyboard, {}, "SpaceBar"}, {}, 1.0},
    {FlightControlAxis::ElevatorTrim, {InputDeviceClass::Keyboard, {}, "KeyPageUp"}, {}, 1.0},
    {FlightControlAxis::ElevatorTrim, {InputDeviceClass::Keyboard, {}, "KeyPageDown"}, {}, -1.0},
    {FlightControlAxis::ViewPanX, {InputDeviceClass::Mouse, {}, "MouseX"}, {}, 1.0},
    {FlightControlAxis::ViewPanY, {InputDeviceClass::Mouse, {}, "MouseY"}, {}, 1.0},
    {FlightControlAxis::ViewZoom, {InputDeviceClass::Mouse, {}, "MouseWheel"}, {}, 1.0},
  };
  keyboard_mouse.command_bindings = {
    {FlightCommand::ParkingBrakeToggle, {InputDeviceClass::Keyboard, {}, "KeyB"}, 0.5},
    {FlightCommand::BrakesHold, {InputDeviceClass::Keyboard, {}, "SpaceBar"}, 0.5},
    {FlightCommand::FlapsUp, {InputDeviceClass::Keyboard, {}, "KeyF"}, 0.5},
    {FlightCommand::FlapsDown, {InputDeviceClass::Keyboard, {}, "KeyV"}, 0.5},
    {FlightCommand::GearToggle, {InputDeviceClass::Keyboard, {}, "KeyG"}, 0.5},
    {FlightCommand::StarterToggle, {InputDeviceClass::Keyboard, {}, "KeyR"}, 0.5},
    {FlightCommand::MasterBatteryToggle, {InputDeviceClass::Keyboard, {}, "KeyM"}, 0.5},
    {FlightCommand::AvionicsToggle, {InputDeviceClass::Keyboard, {}, "KeyO"}, 0.5},
    {FlightCommand::MixtureCutoff, {InputDeviceClass::Keyboard, {}, "Digit1"}, 0.5},
    {FlightCommand::MixtureRich, {InputDeviceClass::Keyboard, {}, "Digit2"}, 0.5},
    {FlightCommand::PropellerFeatherToggle, {InputDeviceClass::Keyboard, {}, "KeyP"}, 0.5},
    {FlightCommand::TrimReset, {InputDeviceClass::Keyboard, {}, "Home"}, 0.5},
    {FlightCommand::ViewReset, {InputDeviceClass::Mouse, {}, "MouseMiddle"}, 0.5},
    {FlightCommand::PauseToggle, {InputDeviceClass::Keyboard, {}, "Escape"}, 0.5},
  };

  InputDeviceProfile gamepad{};
  gamepad.profile_id = "xinput-gamepad-default";
  gamepad.display_name = "Gamepad";
  gamepad.device_class = InputDeviceClass::Gamepad;
  gamepad.axis_bindings = {
    {FlightControlAxis::Pitch, {InputDeviceClass::Gamepad, {}, "RightY"}, {0.08, 1.2, true, 0.95, 0.95}, 1.0},
    {FlightControlAxis::Roll, {InputDeviceClass::Gamepad, {}, "LeftX"}, {0.06, 1.15, false, 0.98, 0.98}, 1.0},
    {FlightControlAxis::Yaw, {InputDeviceClass::Gamepad, {}, "RightX"}, {0.08, 1.2, false, 0.95, 0.95}, 1.0},
    {FlightControlAxis::Throttle, {InputDeviceClass::Gamepad, {}, "RightTrigger"}, {}, 1.0},
    {FlightControlAxis::BrakeCombined, {InputDeviceClass::Gamepad, {}, "LeftTrigger"}, {}, 1.0},
    {FlightControlAxis::ElevatorTrim, {InputDeviceClass::Gamepad, {}, "DPadY"}, {}, 1.0},
    {FlightControlAxis::ViewPanX, {InputDeviceClass::Gamepad, {}, "LeftX"}, {0.2, 1.0, false, 1.0, 1.0}, 0.4},
    {FlightControlAxis::ViewPanY, {InputDeviceClass::Gamepad, {}, "LeftY"}, {0.2, 1.0, false, 1.0, 1.0}, 0.4},
  };
  gamepad.command_bindings = {
    {FlightCommand::ParkingBrakeToggle, {InputDeviceClass::Gamepad, {}, "ButtonB"}, 0.5},
    {FlightCommand::StarterToggle, {InputDeviceClass::Gamepad, {}, "ButtonY"}, 0.5},
    {FlightCommand::ViewReset, {InputDeviceClass::Gamepad, {}, "ButtonRightStick"}, 0.5},
    {FlightCommand::PauseToggle, {InputDeviceClass::Gamepad, {}, "ButtonStart"}, 0.5},
  };

  InputDeviceProfile hid{};
  hid.profile_id = "hid-flight-controls-default";
  hid.display_name = "USB/HID Flight Controls";
  hid.device_class = InputDeviceClass::HidFlightControl;
  hid.axis_bindings = {
    {FlightControlAxis::Pitch, {InputDeviceClass::HidFlightControl, {}, "AxisY"}, {0.02, 1.0, true, 1.0, 1.0}, 1.0},
    {FlightControlAxis::Roll, {InputDeviceClass::HidFlightControl, {}, "AxisX"}, {0.02, 1.0, false, 1.0, 1.0}, 1.0},
    {FlightControlAxis::Yaw, {InputDeviceClass::HidFlightControl, {}, "AxisRz"}, {0.04, 1.0, false, 1.0, 1.0}, 1.0},
    {FlightControlAxis::Throttle, {InputDeviceClass::HidFlightControl, {}, "Slider0"}, {}, 1.0},
    {FlightControlAxis::Mixture, {InputDeviceClass::HidFlightControl, {}, "Slider1"}, {}, 1.0},
    {FlightControlAxis::Propeller, {InputDeviceClass::HidFlightControl, {}, "Slider2"}, {}, 1.0},
    {FlightControlAxis::BrakeLeft, {InputDeviceClass::HidFlightControl, {}, "BrakeLeft"}, {}, 1.0},
    {FlightControlAxis::BrakeRight, {InputDeviceClass::HidFlightControl, {}, "BrakeRight"}, {}, 1.0},
    {FlightControlAxis::BrakeCombined, {InputDeviceClass::HidFlightControl, {}, "BrakeCombined"}, {}, 1.0},
    {FlightControlAxis::ElevatorTrim, {InputDeviceClass::HidFlightControl, {}, "TrimWheel"}, {0.01, 1.0, false, 1.0, 1.0}, 1.0},
    {FlightControlAxis::AileronTrim, {InputDeviceClass::HidFlightControl, {}, "TrimAileron"}, {0.01, 1.0, false, 1.0, 1.0}, 1.0},
    {FlightControlAxis::RudderTrim, {InputDeviceClass::HidFlightControl, {}, "TrimRudder"}, {0.01, 1.0, false, 1.0, 1.0}, 1.0},
    {FlightControlAxis::ViewPanX, {InputDeviceClass::HidFlightControl, {}, "HatX"}, {}, 1.0},
    {FlightControlAxis::ViewPanY, {InputDeviceClass::HidFlightControl, {}, "HatY"}, {}, 1.0},
    {FlightControlAxis::ViewZoom, {InputDeviceClass::HidFlightControl, {}, "HatZoom"}, {}, 1.0},
  };
  hid.command_bindings = {
    {FlightCommand::ParkingBrakeToggle, {InputDeviceClass::HidFlightControl, {}, "ButtonParkingBrake"}, 0.5},
    {FlightCommand::FlapsUp, {InputDeviceClass::HidFlightControl, {}, "ButtonFlapsUp"}, 0.5},
    {FlightCommand::FlapsDown, {InputDeviceClass::HidFlightControl, {}, "ButtonFlapsDown"}, 0.5},
    {FlightCommand::StarterToggle, {InputDeviceClass::HidFlightControl, {}, "ButtonStarter"}, 0.5},
    {FlightCommand::MasterBatteryToggle, {InputDeviceClass::HidFlightControl, {}, "ButtonBattery"}, 0.5},
  };

  settings.profiles = {keyboard_mouse, gamepad, hid};
  return settings;
}

std::vector<std::string> validate_input_settings(const InputSettings& settings) {
  std::vector<std::string> errors;
  if (settings.schema_version != kInputSettingsSchemaVersion) {
    errors.push_back("schema_version must be flying.input-settings.v1");
  }
  if (settings.profiles.empty()) {
    errors.push_back("at least one input device profile is required");
  }
  if (contains_record_delimiter(settings.active_profile_id)) {
    errors.push_back("active_profile_id" + record_delimiter_error_suffix());
  }

  std::unordered_set<std::string> profile_ids;
  for (const InputDeviceProfile& profile : settings.profiles) {
    const std::string context = "profile " + profile.profile_id;
    if (profile.profile_id.empty()) {
      errors.push_back("profile_id must not be empty");
    }
    if (contains_record_delimiter(profile.profile_id) ||
        contains_record_delimiter(profile.display_name) ||
        contains_record_delimiter(profile.hardware_id)) {
      errors.push_back(context + " metadata" + record_delimiter_error_suffix());
    }
    if (!profile.profile_id.empty() && !profile_ids.insert(profile.profile_id).second) {
      errors.push_back("duplicate profile_id " + profile.profile_id);
    }

    for (const AxisBinding& binding : profile.axis_bindings) {
      const std::string binding_context =
          context + " axis " + std::string(to_string(binding.axis));
      if (binding.source.control_path.empty()) {
        errors.push_back(binding_context + " control_path must not be empty");
      }
      if (contains_record_delimiter(binding.source.device_id) ||
          contains_record_delimiter(binding.source.control_path)) {
        errors.push_back(binding_context + " source fields" + record_delimiter_error_suffix());
      }
      if (!is_finite(binding.scale) || binding.scale < -4.0 || binding.scale > 4.0) {
        errors.push_back(binding_context + " scale must be finite and in [-4, 4]");
      }
      add_calibration_error(binding.calibration, binding_context, errors);
    }

    for (const CommandBinding& binding : profile.command_bindings) {
      const std::string binding_context =
          context + " command " + std::string(to_string(binding.command));
      if (binding.source.control_path.empty()) {
        errors.push_back(binding_context + " control_path must not be empty");
      }
      if (contains_record_delimiter(binding.source.device_id) ||
          contains_record_delimiter(binding.source.control_path)) {
        errors.push_back(binding_context + " source fields" + record_delimiter_error_suffix());
      }
      if (!is_finite(binding.activation_threshold) ||
          binding.activation_threshold < 0.0 ||
          binding.activation_threshold > 1.0) {
        errors.push_back(binding_context + " activation_threshold must be finite and in [0, 1]");
      }
    }
  }

  if (!settings.active_profile_id.empty() &&
      profile_ids.find(settings.active_profile_id) == profile_ids.end()) {
    errors.push_back("active_profile_id must reference a profile");
  }

  return errors;
}

InputSettingsLoadResult load_input_settings(const std::filesystem::path& path) {
  InputSettingsLoadResult result{};

  try {
    std::ifstream input(path);
    if (!input) {
      result.errors.push_back("unable to open input settings file");
      return result;
    }

    std::string header;
    if (!std::getline(input, header) || header != kInputSettingsSchemaVersion) {
      result.errors.push_back("input settings schema header is missing or unsupported");
      return result;
    }

    InputSettings settings{};
    settings.schema_version = std::string(kInputSettingsSchemaVersion);

    std::string line;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
      ++line_number;
      if (line.empty()) {
        continue;
      }

      const std::vector<std::string_view> fields = split_fields(line);
      if (fields.empty()) {
        continue;
      }

      if (fields[0] == "active") {
        if (fields.size() != 2) {
          result.errors.push_back("line " + std::to_string(line_number) + " active record is malformed");
          continue;
        }
        settings.active_profile_id = to_owned(fields[1]);
      } else if (fields[0] == "profile") {
        if (fields.size() != 5) {
          result.errors.push_back("line " + std::to_string(line_number) + " profile record is malformed");
          continue;
        }
        const auto device_class = input_device_class_from_string(fields[3]);
        if (!device_class) {
          result.errors.push_back("line " + std::to_string(line_number) + " profile device class is unsupported");
          continue;
        }

        settings.profiles.push_back({
          to_owned(fields[1]),
          to_owned(fields[2]),
          *device_class,
          to_owned(fields[4]),
          {},
          {},
        });
      } else if (fields[0] == "axis") {
        if (fields.size() != 12) {
          result.errors.push_back("line " + std::to_string(line_number) + " axis record is malformed");
          continue;
        }

        InputDeviceProfile* profile = find_profile(settings, fields[1]);
        const auto axis = flight_control_axis_from_string(fields[2]);
        const auto device_class = input_device_class_from_string(fields[3]);
        AxisBinding binding{};
        bool inverted = false;
        if (!profile || !axis || !device_class ||
            !parse_double(fields[6], binding.scale) ||
            !parse_double(fields[7], binding.calibration.dead_zone_norm) ||
            !parse_double(fields[8], binding.calibration.response_curve) ||
            !parse_bool(fields[9], inverted) ||
            !parse_double(fields[10], binding.calibration.saturation_negative_norm) ||
            !parse_double(fields[11], binding.calibration.saturation_positive_norm)) {
          result.errors.push_back("line " + std::to_string(line_number) + " axis record is invalid");
          continue;
        }

        binding.axis = *axis;
        binding.source = {*device_class, to_owned(fields[4]), to_owned(fields[5])};
        binding.calibration.inverted = inverted;
        if (!replace_existing_target_axis(*profile, binding)) {
          profile->axis_bindings.push_back(binding);
        }
      } else if (fields[0] == "command") {
        if (fields.size() != 7) {
          result.errors.push_back("line " + std::to_string(line_number) + " command record is malformed");
          continue;
        }

        InputDeviceProfile* profile = find_profile(settings, fields[1]);
        const auto command = flight_command_from_string(fields[2]);
        const auto device_class = input_device_class_from_string(fields[3]);
        CommandBinding binding{};
        if (!profile || !command || !device_class ||
            !parse_double(fields[6], binding.activation_threshold)) {
          result.errors.push_back("line " + std::to_string(line_number) + " command record is invalid");
          continue;
        }

        binding.command = *command;
        binding.source = {*device_class, to_owned(fields[4]), to_owned(fields[5])};
        if (!replace_existing_command(*profile, binding)) {
          profile->command_bindings.push_back(binding);
        }
      } else {
        result.errors.push_back("line " + std::to_string(line_number) + " record type is unsupported");
      }
    }

    const std::vector<std::string> validation_errors = validate_input_settings(settings);
    result.errors.insert(result.errors.end(), validation_errors.begin(), validation_errors.end());
    if (!result.errors.empty()) {
      return result;
    }

    result.loaded = true;
    result.settings = std::move(settings);
    return result;
  } catch (const std::exception& error) {
    result.errors.push_back(error.what());
    return result;
  }
}

InputSettingsWriteResult save_input_settings_atomic(const std::filesystem::path& path,
                                                    const InputSettings& settings) {
  InputSettingsWriteResult result{};
  result.errors = validate_input_settings(settings);
  if (!result.errors.empty()) {
    return result;
  }

  std::filesystem::path temp;
  try {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
      std::error_code create_error;
      std::filesystem::create_directories(parent, create_error);
      if (create_error) {
        result.errors.push_back("unable to create input settings directory: " + create_error.message());
        return result;
      }
    }

    temp = temp_path_for(path);
    {
      std::ofstream output(temp, std::ios::trunc);
      if (!output) {
        remove_temp_file(temp);
        result.errors.push_back("unable to open temporary input settings file");
        return result;
      }

      output << kInputSettingsSchemaVersion << '\n';
      output << "active|" << settings.active_profile_id << '\n';
      for (const InputDeviceProfile& profile : settings.profiles) {
        append_profile(output, profile);
      }
      output.flush();
      if (!output) {
        remove_temp_file(temp);
        result.errors.push_back("failed while writing temporary input settings file");
        return result;
      }
    }

    std::error_code replace_error;
    if (!replace_file_atomically(temp, path, replace_error)) {
      remove_temp_file(temp);
      result.errors.push_back("unable to atomically replace input settings file: " + replace_error.message());
      return result;
    }

    result.saved = true;
    return result;
  } catch (const std::exception& error) {
    remove_temp_file(temp);
    result.errors.push_back(error.what());
    return result;
  }
}

} // namespace flying::core_sim
