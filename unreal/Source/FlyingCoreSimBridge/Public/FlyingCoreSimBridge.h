#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

struct FLYINGCORESIMBRIDGE_API FFlyingCoreSimImmutableStateSnapshot
{
  bool bValid = false;
  double SimulationTimeSeconds = 0.0;
  int64 StepIndex = 0;
  int64 StateHash = 0;
  FVector EcefPositionMeters = FVector::ZeroVector;
  FVector EcefVelocityMetersPerSecond = FVector::ZeroVector;
  FQuat BodyToEcef = FQuat::Identity;
};

class FLYINGCORESIMBRIDGE_API FFlyingCoreSimBridgeModule final : public IModuleInterface
{
public:
  static FFlyingCoreSimBridgeModule& Get();
  static bool IsAvailable();

  void StartupModule() override;
  void ShutdownModule() override;
};
