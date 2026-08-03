#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlyingCoreSimStateSnapshot.h"
#include "FlyingInputMappingTypes.h"
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

  UFUNCTION(BlueprintPure, Category="Flying|CoreSim")
  const FFlyingCoreSimStateSnapshot& GetCurrentSnapshot() const;

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
  bool ExportTelemetryCsv(const FString& OutputPath);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool ExportTelemetryJson(const FString& OutputPath);

private:
  void EnsureBridge();
  void PublishSnapshot();
  void PublishTelemetryStatus();

  TUniquePtr<FFlyingCoreSimBridgeImpl> Bridge;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim", meta=(AllowPrivateAccess="true"))
  FFlyingCoreSimStateSnapshot CurrentSnapshot;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario", meta=(AllowPrivateAccess="true"))
  FFlyingScenarioRuntimeState CurrentScenarioState;
};
