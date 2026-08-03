#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FlyingCoreSimStateSnapshot.h"

#include "FlyingCoreSimAircraftActor.generated.h"

class UFlyingCesiumGeoreferenceComponent;
class UFlyingCoreSimComponent;
class UStaticMeshComponent;

UCLASS()
class FLYINGPRESENTATION_API AFlyingCoreSimAircraftActor : public AActor
{
  GENERATED_BODY()

public:
  AFlyingCoreSimAircraftActor();

  void BeginPlay() override;
  void Tick(float DeltaSeconds) override;
  void ApplyWorldOffset(const FVector& InOffset, bool bWorldShift) override;

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void UpdatePresentationFromCoreSim();

  UFUNCTION(BlueprintCallable, Category="Flying|CoreSim")
  void UpdatePresentationFromSnapshot(const FFlyingCoreSimStateSnapshot& Snapshot);

private:
  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<USceneComponent> SceneRoot;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UStaticMeshComponent> AircraftMesh;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UFlyingCoreSimComponent> CoreSimComponent;

  UPROPERTY(VisibleAnywhere, Category="Flying|Components")
  TObjectPtr<UFlyingCesiumGeoreferenceComponent> GeoreferenceComponent;
};
