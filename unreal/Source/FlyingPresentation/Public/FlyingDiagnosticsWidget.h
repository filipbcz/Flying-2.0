#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlyingCoreSimStateSnapshot.h"
#include "FlyingInputMappingTypes.h"

#include "FlyingDiagnosticsWidget.generated.h"

class UFlyingCoreSimComponent;

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingDiagnosticsSnapshot
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  bool bValid = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  double LatitudeDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  double LongitudeDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  double EllipsoidalAltitudeMeters = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  double IndicatedAltitudeMeters = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  double AglAltitudeMeters = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  double SimulationTimeSeconds = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  int64 CoreSimStepIndex = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  int64 StateHash = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  double FixedStepSeconds = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FString TerrainSourceTile;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FFlyingWeatherSnapshot Weather;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FFlyingMappedInputState InputState;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance")
  double AverageFrameRate = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance")
  double OnePercentLowFrameRate = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance")
  double LastCoreSimInputProcessingMilliseconds = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance")
  double MaxObservedHitchMilliseconds = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance")
  int64 CoreSimMissedStepCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance")
  int32 MaxCoreSimStepsPerFrame = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance")
  double RamBudgetGiB = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance")
  double VramBudgetGiB = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FString BuildVersion;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FString BuildId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FString AboutBuildSummary;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FString CoreSimVersion;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FString DataVersions;
};

UCLASS(BlueprintType, Blueprintable)
class FLYINGPRESENTATION_API UFlyingDiagnosticsWidget : public UUserWidget
{
  GENERATED_BODY()

public:
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Diagnostics")
  bool bDiagnosticsVisible = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FFlyingDiagnosticsSnapshot Diagnostics;

  UFUNCTION(BlueprintCallable, Category="Flying|Diagnostics")
  void SetDiagnosticsVisible(bool bVisible);

  UFUNCTION(BlueprintCallable, Category="Flying|Diagnostics")
  bool RefreshDiagnostics(UFlyingCoreSimComponent* CoreSimComponent);
};
