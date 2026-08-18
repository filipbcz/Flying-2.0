#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "FlyingOfflinePilotTerrainActor.generated.h"

class UFlyingCesiumGeoreferenceComponent;
class UMaterialInterface;
class UProceduralMeshComponent;

UCLASS()
class FLYINGPRESENTATION_API AFlyingOfflinePilotTerrainActor : public AActor
{
  GENERATED_BODY()

public:
  AFlyingOfflinePilotTerrainActor();

  void BeginPlay() override;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Offline Packages")
  bool bLoadOnBeginPlay = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Offline Packages")
  FString TerrainPackageManifestPath;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Offline Packages")
  FString PilotRegionPackageManifestPath;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Offline Packages")
  int32 RenderLodLevel = -1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Performance")
  int32 MaxTerrainSectionsPerLoad = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Performance")
  int32 MaxTerrainVerticesPerSection = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Performance")
  FVector2D TerrainStreamingFocusLocalMeters = FVector2D::ZeroVector;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Offline Packages")
  TObjectPtr<UMaterialInterface> VertexColorMaterial;

  UFUNCTION(BlueprintCallable, Category="Flying|Offline Packages")
  bool LoadOfflinePackages();

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  int32 GetRenderedTerrainSectionCount() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  bool HasRenderedTerrainSections() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  FString GetLoadedTerrainPackageManifestPath() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  FString GetLoadedPilotRegionPackageManifestPath() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  int32 GetLoadedTerrainTileCount() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  int32 GetLoadedImageryTileCount() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  int32 GetLastRenderedVertexCount() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  int32 GetLastRenderedTriangleCount() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  FVector GetFirstRenderedEcefPositionMeters() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  FVector GetFirstRenderedUnrealPosition() const;

  UFUNCTION(BlueprintPure, Category="Flying|Offline Packages")
  bool DidLastLoadUseRemoteMapDependencies() const;

private:
  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UProceduralMeshComponent> TerrainMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UFlyingCesiumGeoreferenceComponent> GeoreferenceComponent;

  FString LastLoadedTerrainPackageManifestPath;
  FString LastLoadedPilotRegionPackageManifestPath;
  int32 LastLoadedTerrainTileCount = 0;
  int32 LastLoadedImageryTileCount = 0;
  int32 LastRenderedVertexCount = 0;
  int32 LastRenderedTriangleCount = 0;
  FVector FirstRenderedEcefPositionMeters = FVector::ZeroVector;
  FVector FirstRenderedUnrealPosition = FVector::ZeroVector;
  bool bLastLoadUsedRemoteMapDependencies = true;
};
