#pragma once

#include "CoreMinimal.h"
#include "FlyingCoreSimStateSnapshot.h"

#include "FlyingScenarioTypes.generated.h"

UENUM(BlueprintType)
enum class EFlyingScenarioStartMode : uint8
{
  ColdAndDark UMETA(DisplayName="Cold and Dark"),
  ReadyToTaxi UMETA(DisplayName="Ready to Taxi"),
  Airborne UMETA(DisplayName="Airborne")
};

UENUM(BlueprintType)
enum class EFlyingScenarioPositionMode : uint8
{
  AirportOrRunway UMETA(DisplayName="Airport or Runway"),
  GeographicPosition UMETA(DisplayName="Geographic Position")
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingScenarioLocation
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  FName LocationId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  FString AerodromeId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  FString RunwayEndId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  FString DisplayName;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  double LatitudeDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  double LongitudeDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  double ElevationMeters = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  double TrueHeadingDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  bool bSelectable = true;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingScenarioSelection
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FName LocationId = FName(TEXT("FPPV-RWY-09"));

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  EFlyingScenarioStartMode StartMode = EFlyingScenarioStartMode::ReadyToTaxi;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingScenarioFailureSelection
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FName FailureId;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  bool bFailed = false;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingScenarioEditorData
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FString ScenarioName = TEXT("Untitled Scenario");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FString AircraftId = TEXT("flying_trainer_one");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  EFlyingScenarioPositionMode PositionMode = EFlyingScenarioPositionMode::AirportOrRunway;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FFlyingScenarioSelection AirportSelection;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  double LatitudeDegrees = 49.2;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  double LongitudeDegrees = 14.5;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  double AltitudeMeters = 430.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  double TrueHeadingDegrees = 90.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FDateTime LocalDate = FDateTime(2026, 8, 7);

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario", meta=(ClampMin="0.0", ClampMax="86399.0"))
  double LocalTimeSeconds = 43200.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  FFlyingManualWeatherScenario Weather;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Aircraft", meta=(ClampMin="0.0"))
  double PilotAndPayloadWeightKg = 180.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Aircraft", meta=(ClampMin="0.0"))
  double FuelWeightKg = 80.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Aircraft")
  TArray<FFlyingScenarioFailureSelection> Failures;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingScenarioRuntimeState
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  FFlyingScenarioSelection Selection;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  FFlyingScenarioLocation Location;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  bool bBatteryOn = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  bool bEngineRunning = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  bool bAvionicsOn = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  bool bParkingBrakeSet = false;
};
