#pragma once

#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/math.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flying::core_sim {

struct ControlInputSample {
  Vector3d force_body_n{};
  Vector3d moment_body_nm{};
};

struct CallerFrameInput {
  double elapsed_time_s{};
  ControlInputSample controls{};
};

enum class InputDeviceClass {
  Keyboard,
  Mouse,
  Gamepad,
  HidFlightControl,
};

enum class FlightControlAxis {
  Pitch,
  Roll,
  Yaw,
  Throttle,
  Mixture,
  Propeller,
  BrakeLeft,
  BrakeRight,
  BrakeCombined,
  ElevatorTrim,
  AileronTrim,
  RudderTrim,
  ViewPanX,
  ViewPanY,
  ViewZoom,
};

enum class FlightCommand {
  ParkingBrakeToggle,
  BrakesHold,
  FlapsUp,
  FlapsDown,
  GearToggle,
  StarterToggle,
  MasterBatteryToggle,
  AvionicsToggle,
  MixtureCutoff,
  MixtureRich,
  PropellerFeatherToggle,
  TrimReset,
  ViewReset,
  PauseToggle,
};

struct PhysicalInputBinding {
  InputDeviceClass device_class{InputDeviceClass::Keyboard};
  std::string device_id;
  std::string control_path;
};

struct AxisCalibration {
  double dead_zone_norm{};
  double response_curve{1.0};
  bool inverted{};
  double saturation_negative_norm{1.0};
  double saturation_positive_norm{1.0};
};

struct AxisBinding {
  FlightControlAxis axis{FlightControlAxis::Pitch};
  PhysicalInputBinding source{};
  AxisCalibration calibration{};
  double scale{1.0};
};

struct CommandBinding {
  FlightCommand command{FlightCommand::ParkingBrakeToggle};
  PhysicalInputBinding source{};
  double activation_threshold{0.5};
};

struct InputDeviceProfile {
  std::string profile_id;
  std::string display_name;
  InputDeviceClass device_class{InputDeviceClass::Keyboard};
  std::string hardware_id;
  std::vector<AxisBinding> axis_bindings;
  std::vector<CommandBinding> command_bindings;
};

struct InputSettings {
  std::string schema_version{"flying.input-settings.v1"};
  std::string active_profile_id;
  std::vector<InputDeviceProfile> profiles;
};

struct RawInputControlValue {
  PhysicalInputBinding source{};
  double value{};
};

struct RawInputFrame {
  std::vector<RawInputControlValue> controls;
};

struct FlightCommandEvent {
  FlightCommand command{FlightCommand::ParkingBrakeToggle};
  PhysicalInputBinding source{};
};

struct MappedInputState {
  AircraftControlInputSample aircraft{};
  double view_pan_x_norm{};
  double view_pan_y_norm{};
  double view_zoom_norm{};
  std::vector<FlightCommandEvent> commands;
};

struct InputSettingsLoadResult {
  bool loaded{};
  InputSettings settings{};
  std::vector<std::string> errors;
};

struct InputSettingsWriteResult {
  bool saved{};
  std::vector<std::string> errors;
};

[[nodiscard]] std::string_view to_string(InputDeviceClass value) noexcept;
[[nodiscard]] std::string_view to_string(FlightControlAxis value) noexcept;
[[nodiscard]] std::string_view to_string(FlightCommand value) noexcept;

[[nodiscard]] std::optional<InputDeviceClass> input_device_class_from_string(
    std::string_view value) noexcept;
[[nodiscard]] std::optional<FlightControlAxis> flight_control_axis_from_string(
    std::string_view value) noexcept;
[[nodiscard]] std::optional<FlightCommand> flight_command_from_string(
    std::string_view value) noexcept;

[[nodiscard]] std::vector<FlightControlAxis> required_bindable_axes();
[[nodiscard]] std::vector<FlightCommand> core_cockpit_commands();

[[nodiscard]] double apply_axis_calibration(double raw_value,
                                            const AxisCalibration& calibration) noexcept;
[[nodiscard]] MappedInputState map_input_frame(const InputSettings& settings,
                                               const RawInputFrame& frame);
[[nodiscard]] MappedInputState map_input_frame(const InputDeviceProfile& profile,
                                               const RawInputFrame& frame);

[[nodiscard]] InputSettings make_default_input_settings();
[[nodiscard]] std::vector<std::string> validate_input_settings(const InputSettings& settings);

[[nodiscard]] InputSettingsLoadResult load_input_settings(const std::filesystem::path& path);
[[nodiscard]] InputSettingsWriteResult save_input_settings_atomic(
    const std::filesystem::path& path,
    const InputSettings& settings);

} // namespace flying::core_sim
