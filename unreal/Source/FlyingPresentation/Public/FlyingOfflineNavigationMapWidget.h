#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

#include "FlyingOfflineNavigationMapWidget.generated.h"

class UFlyingCoreSimComponent;

UENUM(BlueprintType)
enum class EFlyingNavigationMapLayer : uint8
{
  Airports UMETA(DisplayName="Airports"),
  Runways UMETA(DisplayName="Runways"),
  Obstacles UMETA(DisplayName="Obstacles"),
  Airspaces UMETA(DisplayName="Airspaces"),
  Labels UMETA(DisplayName="Labels"),
  AircraftPosition UMETA(DisplayName="Aircraft Position"),
  FlightPath UMETA(DisplayName="Flight Path"),
  ReplayTrack UMETA(DisplayName="Replay Track")
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingNavigationMapLayerState
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  EFlyingNavigationMapLayer Layer = EFlyingNavigationMapLayer::Airports;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  bool bVisible = true;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingNavigationMapTilePackage
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  FString LayerId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  FString TileArchivePath;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  FString ResolvedTileArchivePath;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  FString TileArchiveFormat;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  FString AttributionText;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingNavigationMapOverlayPoint
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  double EastMeters = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  double NorthMeters = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  double AltitudeMeters = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  double HeadingDegrees = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  double TimeSeconds = 0.0;
};

UCLASS(BlueprintType, Blueprintable)
class FLYINGPRESENTATION_API UFlyingOfflineNavigationMapWidget : public UUserWidget
{
  GENERATED_BODY()

public:
  UFlyingOfflineNavigationMapWidget(const FObjectInitializer& ObjectInitializer);

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  FString MapManifestPath;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  FString MapStylePath;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  bool bInitialized = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  FString LastStatus;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  FString AttributionText;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Navigation Map")
  TArray<FFlyingNavigationMapTilePackage> LocalTilePackages;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  TArray<FFlyingNavigationMapLayerState> LayerStates;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  FFlyingNavigationMapOverlayPoint AircraftPosition;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  TArray<FFlyingNavigationMapOverlayPoint> FlightPath;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Navigation Map")
  TArray<FFlyingNavigationMapOverlayPoint> ReplayTrack;

  UFUNCTION(BlueprintCallable, Category="Flying|Navigation Map")
  bool InitializeOfflineMap();

  UFUNCTION(BlueprintCallable, Category="Flying|Navigation Map")
  void SetLayerVisible(EFlyingNavigationMapLayer Layer, bool bVisible);

  UFUNCTION(BlueprintPure, Category="Flying|Navigation Map")
  bool IsLayerVisible(EFlyingNavigationMapLayer Layer) const;

  UFUNCTION(BlueprintCallable, Category="Flying|Navigation Map")
  void SetAircraftPositionLocal(const FFlyingNavigationMapOverlayPoint& Position);

  UFUNCTION(BlueprintCallable, Category="Flying|Navigation Map")
  void AppendFlightPathPoint(const FFlyingNavigationMapOverlayPoint& Position);

  UFUNCTION(BlueprintCallable, Category="Flying|Navigation Map")
  void SetFlightPath(const TArray<FFlyingNavigationMapOverlayPoint>& Points);

  UFUNCTION(BlueprintCallable, Category="Flying|Navigation Map")
  void SetReplayTrack(const TArray<FFlyingNavigationMapOverlayPoint>& Points);

  UFUNCTION(BlueprintCallable, Category="Flying|Navigation Map")
  void ClearFlightPath();

  UFUNCTION(BlueprintCallable, Category="Flying|Navigation Map")
  void RenderMapToPaintContext(UPARAM(ref) FPaintContext& Context) const;

protected:
  void NativeConstruct() override;

private:
  void EnsureDefaultLayerStates();
  bool LoadStyle();
  bool LoadManifest();
  void RenderTilePackageLayer(FPaintContext& Context,
                              const FFlyingNavigationMapTilePackage& TilePackage,
                              const FVector2D& Origin) const;
};
