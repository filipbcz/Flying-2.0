#if WITH_DEV_AUTOMATION_TESTS

#include "FlyingOfflinePilotTerrainActor.h"
#include "FlyingPilotGameMode.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlyingGeoreferencedContentRendering, Log, All);

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
  FFlyingGeoreferencedContentRenderingStartupTest,
  "Flying.Presentation.GeoreferencedContent.StartupScenarioRendersOfflineRegionalContent",
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

bool FFlyingGeoreferencedContentRenderingStartupTest::RunTest(const FString& Parameters)
{
  UWorld* World = FindRuntimeWorld();
  TestNotNull(TEXT("startup scenario runtime world"), World);
  if (!World)
  {
    return false;
  }

  AFlyingPilotGameMode* PilotGameMode = World->GetAuthGameMode<AFlyingPilotGameMode>();
  TestNotNull(TEXT("startup scenario uses AFlyingPilotGameMode"), PilotGameMode);

  AFlyingOfflinePilotTerrainActor* TerrainActor = nullptr;
  for (TActorIterator<AFlyingOfflinePilotTerrainActor> It(World); It; ++It)
  {
    TerrainActor = *It;
    break;
  }

  TestNotNull(TEXT("startup scenario has an offline regional terrain actor"), TerrainActor);
  if (!TerrainActor)
  {
    return false;
  }

  const bool bLoadedOfflinePackages = TerrainActor->LoadOfflinePackages();
  TestTrue(TEXT("offline regional terrain and imagery packages load at runtime"), bLoadedOfflinePackages);
  TestTrue(TEXT("offline regional terrain produced rendered mesh sections"), TerrainActor->HasRenderedTerrainSections());
  const int32 RenderedSectionCount = TerrainActor->GetRenderedTerrainSectionCount();
  TestTrue(
    TEXT("offline regional terrain section count is nonzero"),
    RenderedSectionCount > 0);
  UE_LOG(
    LogFlyingGeoreferencedContentRendering,
    Display,
    TEXT("FlyingGeoreferencedContentRendering: renderedTerrainSectionCount=%d"),
    RenderedSectionCount);

  return bLoadedOfflinePackages && TerrainActor->HasRenderedTerrainSections();
}

#endif // WITH_DEV_AUTOMATION_TESTS
