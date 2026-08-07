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

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  bool bCrashTelemetryOptIn = false;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FString StructuredLogPath =
    TEXT("Saved/Flying/Diagnostics/structured-log.jsonl");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Diagnostics")
  FString CrashDiagnosticsDirectory =
    TEXT("Saved/Flying/Diagnostics/Crashes");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Offline Packages")
  FString TerrainPackageManifestPath =
    TEXT("Saved/Flying/PilotRegion/Terrain/terrain-package.json");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Offline Packages")
  FString PilotRegionPackageManifestPath =
    TEXT("Saved/Flying/PilotRegion/GIS/pilot-region-package.json");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Offline Packages")
  FString NavigationMapManifestPath =
    TEXT("Saved/Flying/PilotRegion/Navigation/navigation-map.json");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Offline Packages")
  FString NavigationMapStylePath =
    TEXT("Config/FlyingOfflineNavigationMapStyle.json");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Input")
  FString InputSettingsPath =
    TEXT("Saved/Flying/Input/input-settings.txt");

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Georeference")
  double PilotOriginLatitudeDegrees = 49.2;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Georeference")
  double PilotOriginLongitudeDegrees = 14.5;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Georeference")
  double PilotOriginHeightMeters = 1500.0;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  double DefaultAircraftInitialSpeedEastMetersPerSecond = 35.0;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="1"))
  int32 HighGraphicsTargetFrameRate = 60;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="1"))
  int32 HighGraphicsMinimumOnePercentLowFps = 45;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="0.0"))
  double MaximumInputLatencyMilliseconds = 50.0;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="0.0"))
  double MaximumStreamingHitchMilliseconds = 100.0;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="1.0"))
  double MaximumSoakRamGiB = 24.0;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="1.0"))
  double MaximumSoakVramGiB = 10.0;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="1.0"))
  double SoakDurationHours = 10.0;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="0"))
  int32 HighGraphicsTerrainLodLevel = 1;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="1"))
  int32 MaxTerrainSectionsPerLoad = 24;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="4"))
  int32 MaxTerrainVerticesPerSection = 262144;

  UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Flying|Performance", meta=(ClampMin="0.0", ClampMax="1.0"))
  double OrdinaryTerrainObjectDensityScale = 0.65;
};
