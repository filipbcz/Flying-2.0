#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlyingCoreSimStateSnapshot.h"
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

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void ResetCoreSim();

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void AdvanceCoreSim(double DeltaSeconds);

  UFUNCTION(BlueprintPure, Category="Flying|CoreSim")
  const FFlyingCoreSimStateSnapshot& GetCurrentSnapshot() const;

private:
  void EnsureBridge();
  void PublishSnapshot();

  TUniquePtr<FFlyingCoreSimBridgeImpl> Bridge;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim", meta=(AllowPrivateAccess="true"))
  FFlyingCoreSimStateSnapshot CurrentSnapshot;
};
