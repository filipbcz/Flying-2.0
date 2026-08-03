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
  int32 RenderLodLevel = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Offline Packages")
  TObjectPtr<UMaterialInterface> VertexColorMaterial;

  UFUNCTION(BlueprintCallable, Category="Flying|Offline Packages")
  bool LoadOfflinePackages();

private:
  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UProceduralMeshComponent> TerrainMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UFlyingCesiumGeoreferenceComponent> GeoreferenceComponent;
};
