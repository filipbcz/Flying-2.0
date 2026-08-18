#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlyingCoreSimBridge.h"
#include "FlyingCoreSimStateSnapshot.h"
#include "FlyingInputMappingTypes.h"
#include "FlyingPostFlightTypes.h"
#include "FlyingScenarioTypes.h"
#include "Templates/UniquePtr.h"

#include "FlyingCoreSimComponent.generated.h"

struct FFlyingCoreSimBridgeImpl;

UCLASS(BlueprintType, ClassGroup=(Flying), meta=(BlueprintSpawnableComponent))
class FLYINGPRESENTATION_API UFlyingCoreSimComponent : public UActorComponent
{
  GENERATED_BODY()

public:
  UFlyingCoreSimComponent();
  ~UFlyingCoreSimComponent() override;

  void BeginPlay() override;
  void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
  void TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction) override;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|CoreSim")
  bool bAutoAdvance = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|CoreSim", meta=(ClampMin="0.0"))
  double MaxAdvanceDeltaSeconds = 0.1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|CoreSim")
  double InitialLatitudeDegrees = 49.2;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|CoreSim")
  double InitialLongitudeDegrees = 16.6;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|CoreSim")
  double InitialAltitudeMeters = 1500.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|CoreSim")
  FVector InitialVelocityEnuMetersPerSecond = FVector(35.0, 0.0, 0.0);

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  bool bUseScenarioSelectionOnBeginPlay = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FFlyingScenarioSelection InitialScenario;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Replay")
  bool bTelemetryRecording = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Replay")
  bool bReplayLoaded = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Replay")
  FString LastTelemetryStatus;

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void ResetCoreSim();

  UFUNCTION(BlueprintCallable, Category="Flying|Scenario")
  bool StartScenario(const FFlyingScenarioSelection& Selection);

  UFUNCTION(BlueprintCallable, Category="Flying|Scenario")
  bool StartScenarioAtPosition(
    double LatitudeDegrees,
    double LongitudeDegrees,
    double AltitudeMeters,
    double TrueHeadingDegrees,
    EFlyingScenarioStartMode StartMode);

  UFUNCTION(BlueprintPure, Category="Flying|Scenario")
  TArray<FFlyingScenarioLocation> GetPilotScenarioLocations() const;

  UFUNCTION(BlueprintPure, Category="Flying|Scenario")
  const FFlyingScenarioRuntimeState& GetCurrentScenarioState() const;

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void AdvanceCoreSim(double DeltaSeconds);

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void AdvanceCoreSimWithInputs(
    double DeltaSeconds,
    FVector ForceBodyNewtons,
    FVector MomentBodyNewtonMeters,
    const FFlyingMappedInputState& MappedInputState,
    bool bEngineRunning);

  UFUNCTION(BlueprintCallable, Category="Flying|Aircraft Systems")
  void ApplyMappedAircraftControls(
    const FFlyingMappedInputState& MappedInputState,
    bool bEngineRunning);

  UFUNCTION(BlueprintPure, Category="Flying|CoreSim")
  const FFlyingCoreSimStateSnapshot& GetCurrentSnapshot() const;

  UFUNCTION(BlueprintPure, Category="Flying|Instruments")
  const FFlyingAircraftInstrumentSnapshot& GetCurrentInstrumentSnapshot() const;

  UFUNCTION(BlueprintCallable, Category="Flying|Weather")
  void SetManualWeatherScenario(const FFlyingManualWeatherScenario& WeatherScenario);

  UFUNCTION(BlueprintCallable, Category="Flying|Aircraft Systems")
  void SetAircraftSystemSwitch(FName SwitchId, bool bEnabled);

  UFUNCTION(BlueprintCallable, Category="Flying|Aircraft Systems")
  void SetAircraftFuelSelector(FName SelectorId);

  UFUNCTION(BlueprintCallable, Category="Flying|Aircraft Systems")
  void SetAircraftFailure(FName FailureId, bool bFailed);

  UFUNCTION(BlueprintCallable, Category="Flying|Aircraft")
  void SetAircraftId(const FString& AircraftId);

  UFUNCTION(BlueprintPure, Category="Flying|Aircraft")
  FString GetAircraftId() const;

  UFUNCTION(BlueprintCallable, Category="Flying|Aircraft Systems")
  void SetAircraftFuelWeight(double TotalFuelWeightKg);

  UFUNCTION(BlueprintCallable, Category="Flying|Aircraft Systems")
  void SetAircraftLoadedWeight(double PilotAndPayloadWeightKg);

  UFUNCTION(BlueprintPure, Category="Flying|Aircraft Systems")
  double GetAircraftLoadedWeightKg() const;

  UFUNCTION(BlueprintPure, Category="Flying|Input")
  const FFlyingMappedInputState& GetLastMappedInputState() const;

  UFUNCTION(BlueprintPure, Category="Flying|Performance")
  int64 GetCoreSimMissedStepCount() const;

  UFUNCTION(BlueprintPure, Category="Flying|Performance")
  int32 GetMaxCoreSimStepsPerFrame() const;

  UFUNCTION(BlueprintPure, Category="Flying|Performance")
  double GetAverageFrameRate() const;

  UFUNCTION(BlueprintPure, Category="Flying|Performance")
  double GetOnePercentLowFrameRate() const;

  UFUNCTION(BlueprintPure, Category="Flying|Performance")
  double GetMaxObservedHitchMilliseconds() const;

  UFUNCTION(BlueprintPure, Category="Flying|Performance")
  double GetLastCoreSimInputProcessingMilliseconds() const;

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool StartTelemetryRecording(const FString& OutputPath, const FString& SessionId);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool StopTelemetryRecording();

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool SaveTelemetryRecording(const FString& OutputPath);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool LoadTelemetryReplay(const FString& InputPath, bool bWarnOnIncompatible);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool PlayLoadedTelemetryReplay(bool bWarnOnIncompatible);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool ScrubTelemetryReplayNormalized(double PositionNorm);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool ExportTelemetryCsv(const FString& OutputPath);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool ExportTelemetryJson(const FString& OutputPath);

  UFUNCTION(BlueprintPure, Category="Flying|Post Flight")
  TArray<FFlyingTelemetryRoutePoint> GetTelemetryRoutePoints() const;

  UFUNCTION(BlueprintPure, Category="Flying|Post Flight")
  TArray<FFlyingTelemetryGraphSeries> GetTelemetryGraphSeries() const;

private:
  void EnsureBridge();
  void PublishSnapshot();
  void PublishTelemetryStatus();
  void UpdatePerformanceCounters(double DeltaSeconds);

  TUniquePtr<FFlyingCoreSimBridgeImpl> Bridge;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim", meta=(AllowPrivateAccess="true"))
  FFlyingCoreSimStateSnapshot CurrentSnapshot;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments", meta=(AllowPrivateAccess="true"))
  FFlyingAircraftInstrumentSnapshot CurrentInstrumentSnapshot;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario", meta=(AllowPrivateAccess="true"))
  FFlyingScenarioRuntimeState CurrentScenarioState;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Aircraft", meta=(AllowPrivateAccess="true"))
  FString CurrentAircraftId = TEXT("flying_trainer_one");

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Aircraft Systems", meta=(AllowPrivateAccess="true"))
  double AircraftLoadedWeightKg = 180.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input", meta=(AllowPrivateAccess="true"))
  FFlyingMappedInputState LastMappedInputState;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(AllowPrivateAccess="true"))
  int64 CoreSimMissedStepCount = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(AllowPrivateAccess="true"))
  int32 MaxCoreSimStepsPerFrame = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(AllowPrivateAccess="true"))
  double AverageFrameRate = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(AllowPrivateAccess="true"))
  double OnePercentLowFrameRate = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(AllowPrivateAccess="true"))
  double MaxObservedHitchMilliseconds = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(AllowPrivateAccess="true"))
  double LastCoreSimInputProcessingMilliseconds = 0.0;

  double LastInputSampleSeconds = 0.0;
  double FrameTimeWindowTotalSeconds = 0.0;
  int32 FrameTimeWindowWriteIndex = 0;
  int32 PerformanceCounterFrameIndex = 0;
  TArray<double> FrameTimeWindowSeconds;
};
