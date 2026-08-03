#pragma once

#include "CoreMinimal.h"

#include "FlyingScenarioTypes.generated.h"

UENUM(BlueprintType)
enum class EFlyingScenarioStartMode : uint8
{
  ColdAndDark UMETA(DisplayName="Cold and Dark"),
  ReadyToTaxi UMETA(DisplayName="Ready to Taxi"),
  Airborne UMETA(DisplayName="Airborne")
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
