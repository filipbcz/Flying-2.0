#include "flying/core_sim/input.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using flying::core_sim::AxisCalibration;
using flying::core_sim::AxisBinding;
using flying::core_sim::CommandBinding;
using flying::core_sim::FlightCommand;
using flying::core_sim::FlightControlAxis;
using flying::core_sim::InputDeviceClass;
using flying::core_sim::InputDeviceProfile;
using flying::core_sim::InputSettings;
using flying::core_sim::RawInputFrame;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

bool profile_contains_source_class(const InputDeviceProfile& profile, InputDeviceClass device_class) {
  const auto axis_found = std::any_of(
      profile.axis_bindings.begin(),
      profile.axis_bindings.end(),
      [&](const AxisBinding& binding) {
        return binding.source.device_class == device_class;
      });
  const auto command_found = std::any_of(
      profile.command_bindings.begin(),
      profile.command_bindings.end(),
      [&](const CommandBinding& binding) {
        return binding.source.device_class == device_class;
      });
  return axis_found || command_found;
}

bool profile_contains_axis(const InputDeviceProfile& profile, FlightControlAxis axis) {
  return std::any_of(
      profile.axis_bindings.begin(),
      profile.axis_bindings.end(),
      [&](const AxisBinding& binding) {
        return binding.axis == axis;
      });
}

bool profile_contains_command(const InputDeviceProfile& profile, FlightCommand command) {
  return std::any_of(
      profile.command_bindings.begin(),
      profile.command_bindings.end(),
      [&](const CommandBinding& binding) {
        return binding.command == command;
      });
}

InputDeviceProfile find_profile(const InputSettings& settings, const std::string& profile_id) {
  const auto found = std::find_if(
      settings.profiles.begin(),
      settings.profiles.end(),
      [&](const InputDeviceProfile& profile) {
        return profile.profile_id == profile_id;
      });
  require(found != settings.profiles.end(), "expected default profile is missing");
  return *found;
}

void remove_settings_temp_files(const std::filesystem::path& settings_path) {
  const std::filesystem::path parent = settings_path.parent_path();
  if (parent.empty() || !std::filesystem::exists(parent)) {
    return;
  }

  const std::string prefix = settings_path.filename().string() + ".tmp.";
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(parent)) {
    if (entry.path().filename().string().rfind(prefix, 0) == 0) {
      std::filesystem::remove(entry.path());
    }
  }
}

bool settings_temp_file_exists(const std::filesystem::path& settings_path) {
  const std::filesystem::path parent = settings_path.parent_path();
  if (parent.empty() || !std::filesystem::exists(parent)) {
    return false;
  }

  const std::string prefix = settings_path.filename().string() + ".tmp.";
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(parent)) {
    if (entry.path().filename().string().rfind(prefix, 0) == 0) {
      return true;
    }
  }

  return false;
}

void default_profiles_cover_supported_input_classes_and_controls() {
  const InputSettings settings = flying::core_sim::make_default_input_settings();
  const std::vector<std::string> errors = flying::core_sim::validate_input_settings(settings);
  require(errors.empty(), "default input settings must validate");

  const InputDeviceProfile keyboard_mouse = find_profile(settings, "keyboard-mouse-default");
  const InputDeviceProfile gamepad = find_profile(settings, "xinput-gamepad-default");
  const InputDeviceProfile hid = find_profile(settings, "hid-flight-controls-default");

  require(profile_contains_source_class(keyboard_mouse, InputDeviceClass::Keyboard),
          "keyboard profile must bind keyboard sources");
  require(profile_contains_source_class(keyboard_mouse, InputDeviceClass::Mouse),
          "keyboard profile must bind mouse view sources");
  require(profile_contains_source_class(gamepad, InputDeviceClass::Gamepad),
          "gamepad profile must bind gamepad sources");
  require(profile_contains_source_class(hid, InputDeviceClass::HidFlightControl),
          "HID profile must bind USB/HID flight-control sources");

  for (const FlightControlAxis axis : flying::core_sim::required_bindable_axes()) {
    require(profile_contains_axis(hid, axis),
            "HID profile must cover the required aircraft, trim, brake, and view axes");
  }

  for (const FlightCommand command : flying::core_sim::core_cockpit_commands()) {
    require(profile_contains_command(keyboard_mouse, command) ||
                profile_contains_command(gamepad, command) ||
                profile_contains_command(hid, command),
            "default profiles must expose core cockpit commands");
  }
}

void calibration_applies_dead_zone_curve_inversion_and_saturation() {
  AxisCalibration calibration{};
  calibration.dead_zone_norm = 0.10;
  calibration.response_curve = 2.0;
  calibration.inverted = true;
  calibration.saturation_negative_norm = 0.50;
  calibration.saturation_positive_norm = 0.80;

  require_near(flying::core_sim::apply_axis_calibration(0.05, calibration), 0.0, 1.0e-15,
               "axis values inside the dead zone must map to zero");
  require_near(flying::core_sim::apply_axis_calibration(0.50, calibration), -1.0, 1.0e-15,
               "negative-side saturation after inversion must clamp to full travel");

  calibration.inverted = false;
  const double curved = flying::core_sim::apply_axis_calibration(0.45, calibration);
  require_near(curved, 0.25, 1.0e-12,
               "response curve must shape normalized travel after dead zone removal");
}

void hid_profile_maps_aircraft_view_and_command_inputs() {
  InputSettings settings = flying::core_sim::make_default_input_settings();
  settings.active_profile_id = "hid-flight-controls-default";

  const RawInputFrame frame{{
    {{InputDeviceClass::HidFlightControl, "unit-1", "AxisY"}, -0.35},
    {{InputDeviceClass::HidFlightControl, "unit-1", "AxisX"}, 0.40},
    {{InputDeviceClass::HidFlightControl, "unit-1", "AxisRz"}, -0.25},
    {{InputDeviceClass::HidFlightControl, "unit-1", "Slider0"}, 0.80},
    {{InputDeviceClass::HidFlightControl, "unit-1", "Slider1"}, 0.65},
    {{InputDeviceClass::HidFlightControl, "unit-1", "Slider2"}, 0.55},
    {{InputDeviceClass::HidFlightControl, "unit-1", "BrakeLeft"}, 0.30},
    {{InputDeviceClass::HidFlightControl, "unit-1", "BrakeRight"}, 0.45},
    {{InputDeviceClass::HidFlightControl, "unit-1", "TrimWheel"}, -0.20},
    {{InputDeviceClass::HidFlightControl, "unit-1", "HatX"}, 1.00},
    {{InputDeviceClass::HidFlightControl, "unit-1", "ButtonStarter"}, 1.00},
  }};

  const auto mapped = flying::core_sim::map_input_frame(settings, frame);
  require(mapped.aircraft.elevator_norm > 0.0, "pitch axis must map through HID inversion");
  require_near(mapped.aircraft.aileron_norm, 0.38775510204081637, 1.0e-12,
               "roll axis must apply dead-zone normalized HID travel");
  require(mapped.aircraft.rudder_norm < 0.0, "yaw axis must map to rudder");
  require_near(mapped.aircraft.throttle_norm, 0.80, 1.0e-15, "throttle must map to [0, 1]");
  require_near(mapped.aircraft.mixture_norm, 0.65, 1.0e-15, "mixture must map to [0, 1]");
  require_near(mapped.aircraft.propeller_norm, 0.55, 1.0e-15, "propeller must map to [0, 1]");
  require_near(mapped.aircraft.brake_left_norm, 0.30, 1.0e-15, "left brake must map to [0, 1]");
  require_near(mapped.aircraft.brake_right_norm, 0.45, 1.0e-15, "right brake must map to [0, 1]");
  require(mapped.aircraft.elevator_trim_norm < 0.0, "trim wheel must map to elevator trim");
  require_near(mapped.view_pan_x_norm, 1.0, 1.0e-15, "view hat must map to view pan");
  require(mapped.commands.size() == 1, "starter button must emit one command event");
  require(mapped.commands.front().command == FlightCommand::StarterToggle,
          "starter button must emit the starter command");
}

void input_settings_round_trip_atomically_and_reject_corruption() {
  const std::filesystem::path settings_path =
      std::filesystem::temp_directory_path() / "flying-input-mapping-test-settings.txt";
  std::filesystem::remove(settings_path);
  remove_settings_temp_files(settings_path);

  InputSettings invalid_settings = flying::core_sim::make_default_input_settings();
  invalid_settings.profiles.front().display_name = "Broken\nProfile";
  invalid_settings.profiles.front().axis_bindings.front().source.control_path = "Axis\rPitch";
  const auto rejected_save = flying::core_sim::save_input_settings_atomic(settings_path, invalid_settings);
  require(!rejected_save.saved, "settings with record delimiters in persisted fields must be rejected");
  require(!rejected_save.errors.empty(), "save-time settings rejection must report an error");
  require(!std::filesystem::exists(settings_path), "rejected settings must not create the target file");
  require(!settings_temp_file_exists(settings_path), "rejected settings must not leave a temp file");

  InputSettings settings = flying::core_sim::make_default_input_settings();
  settings.active_profile_id = "hid-flight-controls-default";
  const auto saved = flying::core_sim::save_input_settings_atomic(settings_path, settings);
  require(saved.saved, "valid input settings must be saved atomically");
  require(!settings_temp_file_exists(settings_path), "atomic settings write must not leave a temp file");

  const auto loaded = flying::core_sim::load_input_settings(settings_path);
  require(loaded.loaded, "saved input settings must load");
  require(loaded.settings.active_profile_id == "hid-flight-controls-default",
          "active profile must round-trip through persisted settings");
  require(loaded.settings.profiles.size() == settings.profiles.size(),
          "device profiles must round-trip through persisted settings");

  {
    std::ofstream corrupt(settings_path, std::ios::trunc);
    corrupt << "not the input settings schema\naxis|bad\n";
  }
  const auto rejected = flying::core_sim::load_input_settings(settings_path);
  require(!rejected.loaded, "corrupted input settings must be rejected");
  require(!rejected.errors.empty(), "corrupted input settings rejection must report an error");

  std::filesystem::remove(settings_path);
  remove_settings_temp_files(settings_path);
}

} // namespace

int main() {
  default_profiles_cover_supported_input_classes_and_controls();
  calibration_applies_dead_zone_curve_inversion_and_saturation();
  hid_profile_maps_aircraft_view_and_command_inputs();
  input_settings_round_trip_atomically_and_reject_corruption();
  return 0;
}
