#include "FlyingInputMappingSubsystem.h"

#include "FlyingPresentationSettings.h"
#include "Misc/Paths.h"
#include "flying/core_sim/input.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace {

using flying::core_sim::AxisBinding;
using flying::core_sim::AxisCalibration;
using flying::core_sim::CommandBinding;
using flying::core_sim::FlightCommand;
using flying::core_sim::FlightControlAxis;
using flying::core_sim::InputDeviceClass;
using flying::core_sim::InputDeviceProfile;
using flying::core_sim::InputSettings;
using flying::core_sim::PhysicalInputBinding;

std::string ToStdString(const FString& Value)
{
  return std::string(TCHAR_TO_UTF8(*Value));
}

std::string ToStdString(FName Value)
{
  return ToStdString(Value.ToString());
}

FString ToFString(const std::string& Value)
{
  return FString(UTF8_TO_TCHAR(Value.c_str()));
}

FName ToFName(const std::string& Value)
{
  return FName(*ToFString(Value));
}

InputDeviceClass ToCore(EFlyingInputDeviceClass Value)
{
  switch (Value)
  {
  case EFlyingInputDeviceClass::Keyboard:
    return InputDeviceClass::Keyboard;
  case EFlyingInputDeviceClass::Mouse:
    return InputDeviceClass::Mouse;
  case EFlyingInputDeviceClass::Gamepad:
    return InputDeviceClass::Gamepad;
  case EFlyingInputDeviceClass::HidFlightControl:
    return InputDeviceClass::HidFlightControl;
  }

  return InputDeviceClass::Keyboard;
}

EFlyingInputDeviceClass ToUnreal(InputDeviceClass Value)
{
  switch (Value)
  {
  case InputDeviceClass::Keyboard:
    return EFlyingInputDeviceClass::Keyboard;
  case InputDeviceClass::Mouse:
    return EFlyingInputDeviceClass::Mouse;
  case InputDeviceClass::Gamepad:
    return EFlyingInputDeviceClass::Gamepad;
  case InputDeviceClass::HidFlightControl:
    return EFlyingInputDeviceClass::HidFlightControl;
  }

  return EFlyingInputDeviceClass::Keyboard;
}

FlightControlAxis ToCore(EFlyingFlightControlAxis Value)
{
  switch (Value)
  {
  case EFlyingFlightControlAxis::Pitch:
    return FlightControlAxis::Pitch;
  case EFlyingFlightControlAxis::Roll:
    return FlightControlAxis::Roll;
  case EFlyingFlightControlAxis::Yaw:
    return FlightControlAxis::Yaw;
  case EFlyingFlightControlAxis::Throttle:
    return FlightControlAxis::Throttle;
  case EFlyingFlightControlAxis::Mixture:
    return FlightControlAxis::Mixture;
  case EFlyingFlightControlAxis::Propeller:
    return FlightControlAxis::Propeller;
  case EFlyingFlightControlAxis::BrakeLeft:
    return FlightControlAxis::BrakeLeft;
  case EFlyingFlightControlAxis::BrakeRight:
    return FlightControlAxis::BrakeRight;
  case EFlyingFlightControlAxis::BrakeCombined:
    return FlightControlAxis::BrakeCombined;
  case EFlyingFlightControlAxis::ElevatorTrim:
    return FlightControlAxis::ElevatorTrim;
  case EFlyingFlightControlAxis::AileronTrim:
    return FlightControlAxis::AileronTrim;
  case EFlyingFlightControlAxis::RudderTrim:
    return FlightControlAxis::RudderTrim;
  case EFlyingFlightControlAxis::ViewPanX:
    return FlightControlAxis::ViewPanX;
  case EFlyingFlightControlAxis::ViewPanY:
    return FlightControlAxis::ViewPanY;
  case EFlyingFlightControlAxis::ViewZoom:
    return FlightControlAxis::ViewZoom;
  }

  return FlightControlAxis::Pitch;
}

EFlyingFlightControlAxis ToUnreal(FlightControlAxis Value)
{
  switch (Value)
  {
  case FlightControlAxis::Pitch:
    return EFlyingFlightControlAxis::Pitch;
  case FlightControlAxis::Roll:
    return EFlyingFlightControlAxis::Roll;
  case FlightControlAxis::Yaw:
    return EFlyingFlightControlAxis::Yaw;
  case FlightControlAxis::Throttle:
    return EFlyingFlightControlAxis::Throttle;
  case FlightControlAxis::Mixture:
    return EFlyingFlightControlAxis::Mixture;
  case FlightControlAxis::Propeller:
    return EFlyingFlightControlAxis::Propeller;
  case FlightControlAxis::BrakeLeft:
    return EFlyingFlightControlAxis::BrakeLeft;
  case FlightControlAxis::BrakeRight:
    return EFlyingFlightControlAxis::BrakeRight;
  case FlightControlAxis::BrakeCombined:
    return EFlyingFlightControlAxis::BrakeCombined;
  case FlightControlAxis::ElevatorTrim:
    return EFlyingFlightControlAxis::ElevatorTrim;
  case FlightControlAxis::AileronTrim:
    return EFlyingFlightControlAxis::AileronTrim;
  case FlightControlAxis::RudderTrim:
    return EFlyingFlightControlAxis::RudderTrim;
  case FlightControlAxis::ViewPanX:
    return EFlyingFlightControlAxis::ViewPanX;
  case FlightControlAxis::ViewPanY:
    return EFlyingFlightControlAxis::ViewPanY;
  case FlightControlAxis::ViewZoom:
    return EFlyingFlightControlAxis::ViewZoom;
  }

  return EFlyingFlightControlAxis::Pitch;
}

FlightCommand ToCore(EFlyingFlightCommand Value)
{
  switch (Value)
  {
  case EFlyingFlightCommand::ParkingBrakeToggle:
    return FlightCommand::ParkingBrakeToggle;
  case EFlyingFlightCommand::BrakesHold:
    return FlightCommand::BrakesHold;
  case EFlyingFlightCommand::FlapsUp:
    return FlightCommand::FlapsUp;
  case EFlyingFlightCommand::FlapsDown:
    return FlightCommand::FlapsDown;
  case EFlyingFlightCommand::GearToggle:
    return FlightCommand::GearToggle;
  case EFlyingFlightCommand::StarterToggle:
    return FlightCommand::StarterToggle;
  case EFlyingFlightCommand::MasterBatteryToggle:
    return FlightCommand::MasterBatteryToggle;
  case EFlyingFlightCommand::AvionicsToggle:
    return FlightCommand::AvionicsToggle;
  case EFlyingFlightCommand::MixtureCutoff:
    return FlightCommand::MixtureCutoff;
  case EFlyingFlightCommand::MixtureRich:
    return FlightCommand::MixtureRich;
  case EFlyingFlightCommand::PropellerFeatherToggle:
    return FlightCommand::PropellerFeatherToggle;
  case EFlyingFlightCommand::TrimReset:
    return FlightCommand::TrimReset;
  case EFlyingFlightCommand::ViewReset:
    return FlightCommand::ViewReset;
  case EFlyingFlightCommand::PauseToggle:
    return FlightCommand::PauseToggle;
  }

  return FlightCommand::ParkingBrakeToggle;
}

EFlyingFlightCommand ToUnreal(FlightCommand Value)
{
  switch (Value)
  {
  case FlightCommand::ParkingBrakeToggle:
    return EFlyingFlightCommand::ParkingBrakeToggle;
  case FlightCommand::BrakesHold:
    return EFlyingFlightCommand::BrakesHold;
  case FlightCommand::FlapsUp:
    return EFlyingFlightCommand::FlapsUp;
  case FlightCommand::FlapsDown:
    return EFlyingFlightCommand::FlapsDown;
  case FlightCommand::GearToggle:
    return EFlyingFlightCommand::GearToggle;
  case FlightCommand::StarterToggle:
    return EFlyingFlightCommand::StarterToggle;
  case FlightCommand::MasterBatteryToggle:
    return EFlyingFlightCommand::MasterBatteryToggle;
  case FlightCommand::AvionicsToggle:
    return EFlyingFlightCommand::AvionicsToggle;
  case FlightCommand::MixtureCutoff:
    return EFlyingFlightCommand::MixtureCutoff;
  case FlightCommand::MixtureRich:
    return EFlyingFlightCommand::MixtureRich;
  case FlightCommand::PropellerFeatherToggle:
    return EFlyingFlightCommand::PropellerFeatherToggle;
  case FlightCommand::TrimReset:
    return EFlyingFlightCommand::TrimReset;
  case FlightCommand::ViewReset:
    return EFlyingFlightCommand::ViewReset;
  case FlightCommand::PauseToggle:
    return EFlyingFlightCommand::PauseToggle;
  }

  return EFlyingFlightCommand::ParkingBrakeToggle;
}

PhysicalInputBinding ToCore(const FFlyingPhysicalInputBinding& Value)
{
  return {
    ToCore(Value.DeviceClass),
    ToStdString(Value.DeviceId),
    ToStdString(Value.ControlPath),
  };
}

FFlyingPhysicalInputBinding ToUnreal(const PhysicalInputBinding& Value)
{
  FFlyingPhysicalInputBinding Result;
  Result.DeviceClass = ToUnreal(Value.device_class);
  Result.DeviceId = ToFString(Value.device_id);
  Result.ControlPath = ToFString(Value.control_path);
  return Result;
}

AxisCalibration ToCore(const FFlyingAxisCalibration& Value)
{
  return {
    Value.DeadZoneNorm,
    Value.ResponseCurve,
    Value.bInverted,
    Value.SaturationNegativeNorm,
    Value.SaturationPositiveNorm,
  };
}

FFlyingAxisCalibration ToUnreal(const AxisCalibration& Value)
{
  FFlyingAxisCalibration Result;
  Result.DeadZoneNorm = Value.dead_zone_norm;
  Result.ResponseCurve = Value.response_curve;
  Result.bInverted = Value.inverted;
  Result.SaturationNegativeNorm = Value.saturation_negative_norm;
  Result.SaturationPositiveNorm = Value.saturation_positive_norm;
  return Result;
}

AxisBinding ToCore(const FFlyingAxisBinding& Value)
{
  return {
    ToCore(Value.Axis),
    ToCore(Value.Source),
    ToCore(Value.Calibration),
    Value.Scale,
  };
}

FFlyingAxisBinding ToUnreal(const AxisBinding& Value)
{
  FFlyingAxisBinding Result;
  Result.Axis = ToUnreal(Value.axis);
  Result.Source = ToUnreal(Value.source);
  Result.Calibration = ToUnreal(Value.calibration);
  Result.Scale = Value.scale;
  return Result;
}

CommandBinding ToCore(const FFlyingCommandBinding& Value)
{
  return {
    ToCore(Value.Command),
    ToCore(Value.Source),
    Value.ActivationThreshold,
  };
}

FFlyingCommandBinding ToUnreal(const CommandBinding& Value)
{
  FFlyingCommandBinding Result;
  Result.Command = ToUnreal(Value.command);
  Result.Source = ToUnreal(Value.source);
  Result.ActivationThreshold = Value.activation_threshold;
  return Result;
}

InputDeviceProfile ToCore(const FFlyingInputDeviceProfile& Value)
{
  InputDeviceProfile Result;
  Result.profile_id = ToStdString(Value.ProfileId);
  Result.display_name = ToStdString(Value.DisplayName);
  Result.device_class = ToCore(Value.DeviceClass);
  Result.hardware_id = ToStdString(Value.HardwareId);
  Result.axis_bindings.reserve(Value.AxisBindings.Num());
  for (const FFlyingAxisBinding& Binding : Value.AxisBindings)
  {
    Result.axis_bindings.push_back(ToCore(Binding));
  }
  Result.command_bindings.reserve(Value.CommandBindings.Num());
  for (const FFlyingCommandBinding& Binding : Value.CommandBindings)
  {
    Result.command_bindings.push_back(ToCore(Binding));
  }
  return Result;
}

FFlyingInputDeviceProfile ToUnreal(const InputDeviceProfile& Value)
{
  FFlyingInputDeviceProfile Result;
  Result.ProfileId = ToFName(Value.profile_id);
  Result.DisplayName = ToFString(Value.display_name);
  Result.DeviceClass = ToUnreal(Value.device_class);
  Result.HardwareId = ToFString(Value.hardware_id);
  for (const AxisBinding& Binding : Value.axis_bindings)
  {
    Result.AxisBindings.Add(ToUnreal(Binding));
  }
  for (const CommandBinding& Binding : Value.command_bindings)
  {
    Result.CommandBindings.Add(ToUnreal(Binding));
  }
  return Result;
}

InputSettings ToCore(const FFlyingInputSettings& Value)
{
  InputSettings Result;
  Result.active_profile_id = ToStdString(Value.ActiveProfileId);
  Result.profiles.reserve(Value.Profiles.Num());
  for (const FFlyingInputDeviceProfile& Profile : Value.Profiles)
  {
    Result.profiles.push_back(ToCore(Profile));
  }
  return Result;
}

FFlyingInputSettings ToUnreal(const InputSettings& Value)
{
  FFlyingInputSettings Result;
  Result.ActiveProfileId = ToFName(Value.active_profile_id);
  for (const InputDeviceProfile& Profile : Value.profiles)
  {
    Result.Profiles.Add(ToUnreal(Profile));
  }
  return Result;
}

bool SameSource(const FFlyingPhysicalInputBinding& A, const FFlyingPhysicalInputBinding& B)
{
  return A.DeviceClass == B.DeviceClass &&
         A.DeviceId == B.DeviceId &&
         A.ControlPath == B.ControlPath;
}

FFlyingInputDeviceProfile* FindProfile(FFlyingInputSettings& Settings, FName ProfileId)
{
  return Settings.Profiles.FindByPredicate(
    [ProfileId](const FFlyingInputDeviceProfile& Profile)
    {
      return Profile.ProfileId == ProfileId;
    });
}

void AppendErrors(const std::vector<std::string>& Errors, TArray<FString>& OutErrors)
{
  for (const std::string& Error : Errors)
  {
    OutErrors.Add(ToFString(Error));
  }
}

} // namespace

void UFlyingInputMappingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
  Super::Initialize(Collection);

  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  InputSettingsPath = Settings
    ? Settings->InputSettingsPath
    : FString(TEXT("Saved/Flying/Input/input-settings.txt"));
  ResetToDefaultInputSettings();

  TArray<FString> IgnoredErrors;
  LoadInputSettings(IgnoredErrors);
}

void UFlyingInputMappingSubsystem::ResetToDefaultInputSettings()
{
  InputSettings = ToUnreal(flying::core_sim::make_default_input_settings());
}

bool UFlyingInputMappingSubsystem::LoadInputSettings(TArray<FString>& OutErrors)
{
  OutErrors.Reset();
  const FString ResolvedPath = ResolveInputSettingsPath();
  const auto Loaded = flying::core_sim::load_input_settings(
    std::filesystem::path(ToStdString(ResolvedPath)));
  AppendErrors(Loaded.errors, OutErrors);
  if (!Loaded.loaded)
  {
    return false;
  }

  InputSettings = ToUnreal(Loaded.settings);
  return true;
}

bool UFlyingInputMappingSubsystem::SaveInputSettings(TArray<FString>& OutErrors) const
{
  OutErrors.Reset();
  const auto Saved = flying::core_sim::save_input_settings_atomic(
    std::filesystem::path(ToStdString(ResolveInputSettingsPath())),
    ToCore(InputSettings));
  AppendErrors(Saved.errors, OutErrors);
  return Saved.saved;
}

bool UFlyingInputMappingSubsystem::ValidateInputSettings(TArray<FString>& OutErrors) const
{
  OutErrors.Reset();
  const std::vector<std::string> Errors =
    flying::core_sim::validate_input_settings(ToCore(InputSettings));
  AppendErrors(Errors, OutErrors);
  return Errors.empty();
}

void UFlyingInputMappingSubsystem::SetInputSettingsPath(const FString& NewInputSettingsPath)
{
  InputSettingsPath = NewInputSettingsPath;
}

bool UFlyingInputMappingSubsystem::AddOrReplaceAxisBinding(
  FName ProfileId,
  const FFlyingAxisBinding& Binding)
{
  FFlyingInputDeviceProfile* Profile = FindProfile(InputSettings, ProfileId);
  if (!Profile)
  {
    return false;
  }

  for (FFlyingAxisBinding& Existing : Profile->AxisBindings)
  {
    if (Existing.Axis == Binding.Axis && SameSource(Existing.Source, Binding.Source))
    {
      Existing = Binding;
      return true;
    }
  }

  Profile->AxisBindings.Add(Binding);
  return true;
}

bool UFlyingInputMappingSubsystem::SetAxisCalibration(
  FName ProfileId,
  EFlyingFlightControlAxis Axis,
  const FFlyingPhysicalInputBinding& Source,
  const FFlyingAxisCalibration& Calibration)
{
  FFlyingInputDeviceProfile* Profile = FindProfile(InputSettings, ProfileId);
  if (!Profile)
  {
    return false;
  }

  for (FFlyingAxisBinding& Binding : Profile->AxisBindings)
  {
    if (Binding.Axis == Axis && SameSource(Binding.Source, Source))
    {
      Binding.Calibration = Calibration;
      return true;
    }
  }

  return false;
}

bool UFlyingInputMappingSubsystem::AddOrReplaceCommandBinding(
  FName ProfileId,
  const FFlyingCommandBinding& Binding)
{
  FFlyingInputDeviceProfile* Profile = FindProfile(InputSettings, ProfileId);
  if (!Profile)
  {
    return false;
  }

  for (FFlyingCommandBinding& Existing : Profile->CommandBindings)
  {
    if (Existing.Command == Binding.Command && SameSource(Existing.Source, Binding.Source))
    {
      Existing = Binding;
      return true;
    }
  }

  Profile->CommandBindings.Add(Binding);
  return true;
}

FFlyingMappedInputState UFlyingInputMappingSubsystem::MapRawInputFrame(
  const TArray<FFlyingRawInputControlValue>& RawControls) const
{
  flying::core_sim::RawInputFrame Frame;
  Frame.controls.reserve(RawControls.Num());
  for (const FFlyingRawInputControlValue& Raw : RawControls)
  {
    Frame.controls.push_back({ToCore(Raw.Source), Raw.Value});
  }

  const flying::core_sim::MappedInputState Mapped =
    flying::core_sim::map_input_frame(ToCore(InputSettings), Frame);

  FFlyingMappedInputState Result;
  Result.RollNorm = Mapped.aircraft.aileron_norm;
  Result.PitchNorm = Mapped.aircraft.elevator_norm;
  Result.YawNorm = Mapped.aircraft.rudder_norm;
  Result.ThrottleNorm = Mapped.aircraft.throttle_norm;
  Result.MixtureNorm = Mapped.aircraft.mixture_norm;
  Result.PropellerNorm = Mapped.aircraft.propeller_norm;
  Result.BrakeLeftNorm = Mapped.aircraft.brake_left_norm;
  Result.BrakeRightNorm = Mapped.aircraft.brake_right_norm;
  Result.ElevatorTrimNorm = Mapped.aircraft.elevator_trim_norm;
  Result.AileronTrimNorm = Mapped.aircraft.aileron_trim_norm;
  Result.RudderTrimNorm = Mapped.aircraft.rudder_trim_norm;
  Result.ViewPanXNorm = Mapped.view_pan_x_norm;
  Result.ViewPanYNorm = Mapped.view_pan_y_norm;
  Result.ViewZoomNorm = Mapped.view_zoom_norm;

  for (const flying::core_sim::FlightCommandEvent& Command : Mapped.commands)
  {
    FFlyingCommandEvent Event;
    Event.Command = ToUnreal(Command.command);
    Event.Source = ToUnreal(Command.source);
    Result.Commands.Add(Event);
  }

  return Result;
}

FString UFlyingInputMappingSubsystem::ResolveInputSettingsPath() const
{
  FString Path = InputSettingsPath;
  if (Path.IsEmpty())
  {
    Path = TEXT("Saved/Flying/Input/input-settings.txt");
  }

  if (FPaths::IsRelative(Path))
  {
    return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
  }

  return Path;
}
