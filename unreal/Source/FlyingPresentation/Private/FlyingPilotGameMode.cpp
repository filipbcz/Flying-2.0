#include "FlyingPilotGameMode.h"

#include "CesiumGeoreference.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FlyingCoreSimAircraftActor.h"
#include "FlyingOfflinePilotTerrainActor.h"
#include "FlyingPresentationSettings.h"

void AFlyingPilotGameMode::BeginPlay()
{
  Super::BeginPlay();

  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  if (!Settings->bSpawnDefaultSceneOnBeginPlay || !GetWorld())
  {
    return;
  }

  ACesiumGeoreference* Georeference = ACesiumGeoreference::GetDefaultGeoreference(this);
  if (Georeference)
  {
    Georeference->SetOriginLongitudeLatitudeHeight(FVector(
      Settings->PilotOriginLongitudeDegrees,
      Settings->PilotOriginLatitudeDegrees,
      Settings->PilotOriginHeightMeters));
  }

  bool bHasTerrainActor = false;
  for (TActorIterator<AFlyingOfflinePilotTerrainActor> It(GetWorld()); It; ++It)
  {
    bHasTerrainActor = true;
    break;
  }

  if (!bHasTerrainActor)
  {
    GetWorld()->SpawnActor<AFlyingOfflinePilotTerrainActor>();
  }

  bool bHasAircraftActor = false;
  for (TActorIterator<AFlyingCoreSimAircraftActor> It(GetWorld()); It; ++It)
  {
    bHasAircraftActor = true;
    break;
  }

  if (!bHasAircraftActor)
  {
    GetWorld()->SpawnActor<AFlyingCoreSimAircraftActor>();
  }
}
