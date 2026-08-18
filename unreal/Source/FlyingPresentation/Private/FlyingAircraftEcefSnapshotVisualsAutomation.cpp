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

  Aircraft->ApplyWorldOffset(FVector(10000.0, 1000.0, -250.0), true);
  TestTrue(
    TEXT("origin shifts recompute visuals from the same immutable ECEF snapshot source"),
    Aircraft->DoesVisualPresentationMatchImmutableSnapshot(CoreSim->GetCurrentImmutableSnapshot()));

  Aircraft->Destroy();
  return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
