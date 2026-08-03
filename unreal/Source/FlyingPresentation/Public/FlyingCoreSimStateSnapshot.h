#pragma once

#include "CoreMinimal.h"

#include "FlyingCoreSimStateSnapshot.generated.h"

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingCoreSimStateSnapshot
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  bool bValid = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  double SimulationTimeSeconds = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  int64 StepIndex = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  int64 StateHash = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  FVector EcefPositionMeters = FVector::ZeroVector;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  FVector EcefVelocityMetersPerSecond = FVector::ZeroVector;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  FVector4 BodyToEcefQuaternionXyzw = FVector4(0.0, 0.0, 0.0, 1.0);

  FQuat BodyToEcef = FQuat::Identity;
};
