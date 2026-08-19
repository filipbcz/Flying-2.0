#if WITH_DEV_AUTOMATION_TESTS

#include "FlyingCoreSimAircraftActor.h"
#include "FlyingCoreSimComponent.h"

#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FFlyingAircraftEcefSnapshotVisualsTest,
  "Flying.Presentation.Aircraft.EcefSnapshotDrivesCockpitAndExternalVisuals",
  EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
UWorld* FindRuntimeWorld()
{
  if (!GEngine)
  {
    return nullptr;
  }

  for (const FWorldContext& Context : GEngine->GetWorldContexts())
  {
    UWorld* World = Context.World();
    if (World && (World->IsGameWorld() || World->WorldType == EWorldType::PIE))
    {
      return World;
    }
  }

  return nullptr;
}

bool SameImmutableSnapshotIdentity(
  const FFlyingCoreSimImmutableStateSnapshot& Left,
  const FFlyingCoreSimImmutableStateSnapshot& Right)
{
  return Left.bValid == Right.bValid &&
         Left.StepIndex == Right.StepIndex &&
         Left.StateHash == Right.StateHash &&
         Left.SimulationTimeSeconds == Right.SimulationTimeSeconds;
}
}

bool FFlyingAircraftEcefSnapshotVisualsTest::RunTest(const FString& Parameters)
{
  UWorld* World = FindRuntimeWorld();
  TestNotNull(TEXT("runtime world"), World);
  if (!World)
  {
    return false;
  }

  AFlyingCoreSimAircraftActor* Aircraft =
    World->SpawnActor<AFlyingCoreSimAircraftActor>();
  TestNotNull(TEXT("aircraft actor runtime instance"), Aircraft);
  if (!Aircraft)
  {
    return false;
  }

  UFlyingCoreSimComponent* CoreSim =
    Aircraft->FindComponentByClass<UFlyingCoreSimComponent>();
  TestNotNull(TEXT("aircraft actor owns CoreSim component"), CoreSim);
  if (!CoreSim)
  {
    Aircraft->Destroy();
    return false;
  }

  CoreSim->bAutoAdvance = false;
  CoreSim->ResetCoreSim();

  const FFlyingCoreSimImmutableStateSnapshot PublishedSnapshot =
    CoreSim->GetCurrentImmutableSnapshot();
  TestTrue(TEXT("published immutable ECEF snapshot is valid"), PublishedSnapshot.bValid);

  Aircraft->UpdatePresentationFromImmutableSnapshot(PublishedSnapshot);

  Aircraft->SetCockpitCameraMode(EFlyingCockpitCameraMode::Pilot);
  UCameraComponent* CockpitCamera = Aircraft->GetActiveCameraComponent();
  TestNotNull(TEXT("cockpit view resolves to camera component"), CockpitCamera);

  Aircraft->SetCockpitCameraMode(EFlyingCockpitCameraMode::ExteriorOrbit);
  UCameraComponent* ExternalCamera = Aircraft->GetActiveCameraComponent();
  TestNotNull(TEXT("external view resolves to camera component"), ExternalCamera);
  TestTrue(
    TEXT("cockpit and external camera components are distinct views"),
    CockpitCamera && ExternalCamera && CockpitCamera != ExternalCamera);

  TestTrue(
    TEXT("cockpit and external visuals share snapshot-driven actor root"),
    Aircraft->DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot());
  TestTrue(
    TEXT("aircraft visuals match the published immutable ECEF snapshot"),
    Aircraft->DoesVisualPresentationMatchImmutableSnapshot(PublishedSnapshot));

  const int64 InitialSnapshotStepIndex = PublishedSnapshot.StepIndex;
  const int64 InitialSnapshotStateHash = PublishedSnapshot.StateHash;

  Aircraft->ApplyWorldOffset(FVector(10000.0, 1000.0, -250.0), true);
  const FFlyingCoreSimImmutableStateSnapshot OriginShiftSnapshot =
    CoreSim->GetCurrentImmutableSnapshot();
  TestTrue(
    TEXT("origin shift preserves authoritative immutable ECEF snapshot identity"),
    SameImmutableSnapshotIdentity(PublishedSnapshot, OriginShiftSnapshot));
  TestTrue(
    TEXT("origin shifts recompute visuals from the same immutable ECEF snapshot source"),
    Aircraft->DoesVisualPresentationMatchImmutableSnapshot(OriginShiftSnapshot));

  const double FrameDeltasSeconds[] = {1.0 / 120.0, 1.0 / 30.0, 1.0 / 75.0};
  for (double FrameDeltaSeconds : FrameDeltasSeconds)
  {
    CoreSim->AdvanceCoreSim(FrameDeltaSeconds);
    const FFlyingCoreSimImmutableStateSnapshot FrameRateSnapshot =
      CoreSim->GetCurrentImmutableSnapshot();
    TestTrue(
      TEXT("frame-rate variation advances the authoritative CoreSim snapshot id"),
      FrameRateSnapshot.bValid &&
        FrameRateSnapshot.StepIndex > InitialSnapshotStepIndex &&
        FrameRateSnapshot.StateHash != InitialSnapshotStateHash);

    Aircraft->UpdatePresentationFromImmutableSnapshot(FrameRateSnapshot);
    TestTrue(
      TEXT("frame-rate variation keeps cockpit and external visuals on the same CoreSim snapshot"),
      Aircraft->DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot() &&
        Aircraft->DoesVisualPresentationMatchImmutableSnapshot(FrameRateSnapshot));
  }

  Aircraft->Destroy();
  return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
