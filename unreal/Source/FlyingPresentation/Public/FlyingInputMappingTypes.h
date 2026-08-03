#pragma once

#include "CoreMinimal.h"

#include "FlyingInputMappingTypes.generated.h"

UENUM(BlueprintType)
enum class EFlyingInputDeviceClass : uint8
{
  Keyboard UMETA(DisplayName="Keyboard"),
  Mouse UMETA(DisplayName="Mouse"),
  Gamepad UMETA(DisplayName="Gamepad"),
  HidFlightControl UMETA(DisplayName="USB/HID Flight Control")
};

UENUM(BlueprintType)
enum class EFlyingFlightControlAxis : uint8
{
  Pitch UMETA(DisplayName="Pitch"),
  Roll UMETA(DisplayName="Roll"),
  Yaw UMETA(DisplayName="Yaw"),
  Throttle UMETA(DisplayName="Throttle"),
  Mixture UMETA(DisplayName="Mixture"),
  Propeller UMETA(DisplayName="Propeller"),
  BrakeLeft UMETA(DisplayName="Left Brake"),
  BrakeRight UMETA(DisplayName="Right Brake"),
  BrakeCombined UMETA(DisplayName="Combined Brakes"),
  ElevatorTrim UMETA(DisplayName="Elevator Trim"),
  AileronTrim UMETA(DisplayName="Aileron Trim"),
  RudderTrim UMETA(DisplayName="Rudder Trim"),
  ViewPanX UMETA(DisplayName="View Pan X"),
  ViewPanY UMETA(DisplayName="View Pan Y"),
  ViewZoom UMETA(DisplayName="View Zoom")
};

UENUM(BlueprintType)
enum class EFlyingFlightCommand : uint8
{
  ParkingBrakeToggle UMETA(DisplayName="Parking Brake Toggle"),
  BrakesHold UMETA(DisplayName="Hold Brakes"),
  FlapsUp UMETA(DisplayName="Flaps Up"),
  FlapsDown UMETA(DisplayName="Flaps Down"),
  GearToggle UMETA(DisplayName="Gear Toggle"),
  StarterToggle UMETA(DisplayName="Starter Toggle"),
  MasterBatteryToggle UMETA(DisplayName="Master Battery Toggle"),
  AvionicsToggle UMETA(DisplayName="Avionics Toggle"),
  MixtureCutoff UMETA(DisplayName="Mixture Cutoff"),
  MixtureRich UMETA(DisplayName="Mixture Rich"),
  PropellerFeatherToggle UMETA(DisplayName="Propeller Feather Toggle"),
  TrimReset UMETA(DisplayName="Trim Reset"),
  ViewReset UMETA(DisplayName="View Reset"),
  PauseToggle UMETA(DisplayName="Pause Toggle")
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingPhysicalInputBinding
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  EFlyingInputDeviceClass DeviceClass = EFlyingInputDeviceClass::Keyboard;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FString DeviceId;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FString ControlPath;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingAxisCalibration
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input", meta=(ClampMin="0.0", ClampMax="0.99"))
  double DeadZoneNorm = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input", meta=(ClampMin="0.1", ClampMax="8.0"))
  double ResponseCurve = 1.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  bool bInverted = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input", meta=(ClampMin="0.01", ClampMax="1.0"))
  double SaturationNegativeNorm = 1.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input", meta=(ClampMin="0.01", ClampMax="1.0"))
  double SaturationPositiveNorm = 1.0;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingAxisBinding
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  EFlyingFlightControlAxis Axis = EFlyingFlightControlAxis::Pitch;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FFlyingPhysicalInputBinding Source;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FFlyingAxisCalibration Calibration;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input", meta=(ClampMin="-4.0", ClampMax="4.0"))
  double Scale = 1.0;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingCommandBinding
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  EFlyingFlightCommand Command = EFlyingFlightCommand::ParkingBrakeToggle;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FFlyingPhysicalInputBinding Source;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input", meta=(ClampMin="0.0", ClampMax="1.0"))
  double ActivationThreshold = 0.5;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingInputDeviceProfile
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FName ProfileId = FName(TEXT("keyboard-mouse-default"));

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FString DisplayName;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  EFlyingInputDeviceClass DeviceClass = EFlyingInputDeviceClass::Keyboard;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FString HardwareId;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  TArray<FFlyingAxisBinding> AxisBindings;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  TArray<FFlyingCommandBinding> CommandBindings;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingInputSettings
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FName ActiveProfileId = FName(TEXT("keyboard-mouse-default"));

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  TArray<FFlyingInputDeviceProfile> Profiles;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingRawInputControlValue
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FFlyingPhysicalInputBinding Source;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  double Value = 0.0;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingCommandEvent
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  EFlyingFlightCommand Command = EFlyingFlightCommand::ParkingBrakeToggle;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  FFlyingPhysicalInputBinding Source;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingMappedInputState
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double RollNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double PitchNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double YawNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double ThrottleNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double MixtureNorm = 1.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double PropellerNorm = 1.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double BrakeLeftNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double BrakeRightNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double ElevatorTrimNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double AileronTrimNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double RudderTrimNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double ViewPanXNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double ViewPanYNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  double ViewZoomNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  TArray<FFlyingCommandEvent> Commands;
};
