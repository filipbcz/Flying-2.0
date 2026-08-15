#include "InputProfileSubsystem.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using flying::core_sim::AxisBinding;
using flying::core_sim::CommandBinding;
using flying::core_sim::FlightCommand;
using flying::core_sim::FlightControlAxis;
using flying::core_sim::InputDeviceClass;
using flying::core_sim::InputDeviceProfile;
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

bool contains_device_class(const InputDeviceProfile& profile, InputDeviceClass device_class) {
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

bool contains_axis(const InputDeviceProfile& profile, FlightControlAxis axis) {
  return std::any_of(
      profile.axis_bindings.begin(),
      profile.axis_bindings.end(),
      [&](const AxisBinding& binding) {
        return binding.axis == axis;
      });
}

const InputDeviceProfile& find_profile(
    const flying::presentation::InputProfileSettings& settings,
    const std::string& profile_id) {
  const auto found = std::find_if(
      settings.profiles.begin(),
      settings.profiles.end(),
      [&](const InputDeviceProfile& profile) {
        return profile.profile_id == profile_id;
      });
  require(found != settings.profiles.end(), "expected profile is missing");
  return *found;
}

std::filesystem::path backup_path_for(const std::filesystem::path& path) {
  std::filesystem::path backup = path;
  backup += ".bak";
  return backup;
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  require(static_cast<bool>(input), "expected file must be readable");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void schema_document_matches_profile_contract() {
  const std::filesystem::path schema_path =
      std::filesystem::path(FLYING_REPO_SOURCE_DIR) / "schemas/input_profile.schema.json";
  const std::string schema = read_text(schema_path);
  const auto settings = flying::presentation::make_default_input_profile_settings();
  const std::string document = flying::presentation::serialize_input_profiles_schema_json(settings);

  require(schema.find("\"schemaVersion\"") != std::string::npos,
          "schema must define schemaVersion");
  require(schema.find("\"activeProfileId\"") != std::string::npos,
          "schema must define activeProfileId");
  require(schema.find("\"profiles\"") != std::string::npos,
          "schema must define profiles");
  require(document.find("\"schemaVersion\":\"flying.input-settings.v1\"") != std::string::npos,
          "serialized profile document must use the schema version");
  require(document.find("\"activeProfileId\":\"keyboard-mouse-default\"") != std::string::npos,
          "serialized profile document must include active profile id");

  for (const InputDeviceClass device_class : {
           InputDeviceClass::Keyboard,
           InputDeviceClass::Mouse,
           InputDeviceClass::Gamepad,
           InputDeviceClass::HidFlightControl,
       }) {
    const std::string token = "\"" + std::string(flying::core_sim::to_string(device_class)) + "\"";
    require(schema.find(token) != std::string::npos, "schema must include every device class enum");
    require(document.find(token) != std::string::npos,
            "serialized profile document must include every supported device class");
  }

  for (const FlightControlAxis axis : flying::core_sim::required_bindable_axes()) {
    const std::string token = "\"" + std::string(flying::core_sim::to_string(axis)) + "\"";
    require(schema.find(token) != std::string::npos, "schema must include required axis enum");
    require(document.find(token) != std::string::npos,
            "serialized profile document must include required axis binding");
  }
}

void supported_devices_share_one_profile_contract() {
  const auto settings = flying::presentation::make_default_input_profile_settings();
  const auto errors = flying::presentation::validate_input_profile_settings(settings);
  require(errors.empty(), "default input profile settings must validate");

  const InputDeviceProfile& keyboard_mouse = find_profile(settings, "keyboard-mouse-default");
  const InputDeviceProfile& gamepad = find_profile(settings, "xinput-gamepad-default");
  const InputDeviceProfile& hid = find_profile(settings, "hid-flight-controls-default");

  require(contains_device_class(keyboard_mouse, InputDeviceClass::Keyboard),
          "keyboard bindings must use the shared profile schema");
  require(contains_device_class(keyboard_mouse, InputDeviceClass::Mouse),
          "mouse bindings must use the shared profile schema");
  require(contains_device_class(gamepad, InputDeviceClass::Gamepad),
          "gamepad bindings must use the shared profile schema");
  require(contains_device_class(hid, InputDeviceClass::HidFlightControl),
          "USB/HID flight-control bindings must use the shared profile schema");

  for (const FlightControlAxis axis : flying::core_sim::required_bindable_axes()) {
    require(contains_axis(hid, axis), "HID profile must bind every required aircraft axis");
  }
}

void axis_dead_zone_and_curve_outputs_are_normalized() {
  flying::core_sim::AxisCalibration calibration{};
  calibration.dead_zone_norm = 0.10;
  calibration.response_curve = 2.0;
  calibration.saturation_negative_norm = 1.0;
  calibration.saturation_positive_norm = 1.0;

  require_near(flying::presentation::apply_input_profile_axis_response(0.05, calibration),
               0.0,
               1.0e-15,
               "dead-zone input must normalize to zero");
  require_near(flying::presentation::apply_input_profile_axis_response(0.55, calibration),
               0.25,
               1.0e-12,
               "response curve must shape post-dead-zone travel");

  calibration.inverted = true;
  calibration.saturation_negative_norm = 0.50;
  calibration.saturation_positive_norm = 0.80;
  require_near(flying::presentation::apply_input_profile_axis_response(0.50, calibration),
               -1.0,
               1.0e-15,
               "inverted saturated travel must clamp to normalized full throw");
}

void profile_maps_hid_axes_and_commands() {
  auto settings = flying::presentation::make_default_input_profile_settings();
  settings.active_profile_id = "hid-flight-controls-default";

  const RawInputFrame frame{{
      {{InputDeviceClass::HidFlightControl, "stick-1", "AxisY"}, -0.35},
      {{InputDeviceClass::HidFlightControl, "stick-1", "AxisX"}, 0.40},
      {{InputDeviceClass::HidFlightControl, "stick-1", "Slider0"}, 0.80},
      {{InputDeviceClass::HidFlightControl, "stick-1", "ButtonStarter"}, 1.00},
  }};

  const auto mapped = flying::presentation::map_input_profile_frame(settings, frame);
  require(mapped.aircraft.elevator_norm > 0.0, "pitch must map through HID inversion");
  require_near(mapped.aircraft.aileron_norm,
               0.38775510204081637,
               1.0e-12,
               "roll must apply HID dead-zone normalization");
  require_near(mapped.aircraft.throttle_norm, 0.80, 1.0e-15, "throttle must map to unit range");
  require(mapped.commands.size() == 1, "starter button must produce one command event");
  require(mapped.commands.front().command == FlightCommand::StarterToggle,
          "starter button must map to starter command");
}

void profile_save_load_is_atomic_and_corruption_resistant() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "flying-input-profile-tests.settings";
  std::filesystem::remove(path);
  std::filesystem::remove(backup_path_for(path));

  auto settings = flying::presentation::make_default_input_profile_settings();
  settings.active_profile_id = "hid-flight-controls-default";
  const auto saved = flying::presentation::save_input_profiles_atomic(path, settings);
  require(saved.saved, "valid input profiles must save atomically");

  const auto loaded = flying::presentation::load_input_profiles(path);
  require(loaded.loaded, "saved input profiles must load");
  require(loaded.settings.active_profile_id == "hid-flight-controls-default",
          "active profile must round-trip");
  require(loaded.settings.profiles.size() == settings.profiles.size(),
          "profiles must round-trip");

  auto invalid = settings;
  invalid.profiles.front().display_name = "Corrupt\nName";
  const auto rejected_save = flying::presentation::save_input_profiles_atomic(path, invalid);
  require(!rejected_save.saved, "invalid profile data must be rejected before writing");

  const auto preserved_after_rejected_write = flying::presentation::load_input_profiles(path);
  require(preserved_after_rejected_write.loaded,
          "last valid profile must remain loadable after rejected write");
  require(preserved_after_rejected_write.settings.active_profile_id == "hid-flight-controls-default",
          "rejected write must preserve the last valid active profile");

  {
    std::ofstream corrupt(path, std::ios::trunc);
    corrupt << "flying.input-settings.v1\nactive|missing-after-interruption\n";
  }
  const auto recovered = flying::presentation::load_input_profiles(path);
  require(recovered.loaded, "load must recover the last valid profile after primary corruption");
  require(recovered.settings.active_profile_id == "hid-flight-controls-default",
          "recovery must preserve the last valid active profile");
  require(recovered.settings.profiles.size() == settings.profiles.size(),
          "recovery must preserve the last valid profile set");

  std::filesystem::remove(backup_path_for(path));
  const auto rejected_load = flying::presentation::load_input_profiles(path);
  require(!rejected_load.loaded, "corrupt profile storage without backup must be rejected");
  require(!rejected_load.errors.empty(), "corrupt profile rejection must report errors");

  std::filesystem::remove(path);
  std::filesystem::remove(backup_path_for(path));
}

} // namespace

int main() {
  schema_document_matches_profile_contract();
  supported_devices_share_one_profile_contract();
  axis_dead_zone_and_curve_outputs_are_normalized();
  profile_maps_hid_axes_and_commands();
  profile_save_load_is_atomic_and_corruption_resistant();
  return 0;
}
