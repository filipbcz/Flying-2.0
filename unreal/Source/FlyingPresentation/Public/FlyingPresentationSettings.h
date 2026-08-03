#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "FlyingPresentationSettings.generated.h"

UCLASS(Config=Game, DefaultConfig)
class FLYINGPRESENTATION_API UFlyingPresentationSettings : public UObject
{
  GENERATED_BODY()

public:
  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Runtime")
  bool bOfflineOnly = true;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Runtime")
  bool bSpawnDefaultSceneOnBeginPlay = true;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Offline Packages")
  FString TerrainPackageManifestPath =
    TEXT("Saved/Flying/PilotRegion/Terrain/terrain-package.json");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Offline Packages")
  FString PilotRegionPackageManifestPath =
    TEXT("Saved/Flying/PilotRegion/GIS/pilot-region-package.json");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Georeference")
  double PilotOriginLatitudeDegrees = 49.2;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Georeference")
  double PilotOriginLongitudeDegrees = 16.6;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Georeference")
  double PilotOriginHeightMeters = 1500.0;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  double DefaultAircraftInitialSpeedEastMetersPerSecond = 35.0;
};
