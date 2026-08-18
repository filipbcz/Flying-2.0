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
  TestTrue(
    TEXT("offline regional terrain package manifest path was observed"),
    !TerrainActor->GetLoadedTerrainPackageManifestPath().IsEmpty());
  TestTrue(
    TEXT("offline pilot region package manifest path was observed"),
    !TerrainActor->GetLoadedPilotRegionPackageManifestPath().IsEmpty());
  TestTrue(
    TEXT("offline regional terrain tiles were observed"),
    TerrainActor->GetLoadedTerrainTileCount() > 0);
  TestTrue(
    TEXT("offline regional imagery tiles were observed"),
    TerrainActor->GetLoadedImageryTileCount() > 0);
  TestTrue(
    TEXT("rendered georeferenced terrain vertices were observed"),
    TerrainActor->GetLastRenderedVertexCount() > 0);
  TestTrue(
    TEXT("rendered georeferenced terrain triangles were observed"),
    TerrainActor->GetLastRenderedTriangleCount() > 0);
  TestFalse(
    TEXT("startup load used no external map API or remote tile dependency"),
    TerrainActor->DidLastLoadUseRemoteMapDependencies());

  const FVector FirstEcefPosition = TerrainActor->GetFirstRenderedEcefPositionMeters();
  const FVector FirstUnrealPosition = TerrainActor->GetFirstRenderedUnrealPosition();
  TestFalse(
    TEXT("first rendered ECEF position is observed"),
    FirstEcefPosition.IsNearlyZero());
  UE_LOG(
    LogFlyingGeoreferencedContentRendering,
    Display,
    TEXT("FlyingGeoreferencedContentRendering: renderedTerrainSectionCount=%d terrainTileCount=%d imageryTileCount=%d renderedVertexCount=%d renderedTriangleCount=%d usedRemoteMapDependencies=%s terrainManifestPath=\"%s\" pilotRegionManifestPath=\"%s\" firstEcef=(%.3f,%.3f,%.3f) firstUnreal=(%.3f,%.3f,%.3f)"),
    RenderedSectionCount,
    TerrainActor->GetLoadedTerrainTileCount(),
    TerrainActor->GetLoadedImageryTileCount(),
    TerrainActor->GetLastRenderedVertexCount(),
    TerrainActor->GetLastRenderedTriangleCount(),
    TerrainActor->DidLastLoadUseRemoteMapDependencies() ? TEXT("true") : TEXT("false"),
    *TerrainActor->GetLoadedTerrainPackageManifestPath(),
    *TerrainActor->GetLoadedPilotRegionPackageManifestPath(),
    FirstEcefPosition.X,
    FirstEcefPosition.Y,
    FirstEcefPosition.Z,
    FirstUnrealPosition.X,
    FirstUnrealPosition.Y,
    FirstUnrealPosition.Z);

  return bLoadedOfflinePackages &&
         TerrainActor->HasRenderedTerrainSections() &&
         TerrainActor->GetLoadedTerrainTileCount() > 0 &&
         TerrainActor->GetLoadedImageryTileCount() > 0 &&
         TerrainActor->GetLastRenderedVertexCount() > 0 &&
         TerrainActor->GetLastRenderedTriangleCount() > 0 &&
         !TerrainActor->DidLastLoadUseRemoteMapDependencies() &&
         !FirstEcefPosition.IsNearlyZero();
}

#endif // WITH_DEV_AUTOMATION_TESTS
