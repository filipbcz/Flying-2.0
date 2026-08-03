#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FlyingCoreSimStateSnapshot.h"

#include "FlyingCesiumGeoreferenceComponent.generated.h"

class ACesiumGeoreference;

UCLASS(BlueprintType, ClassGroup=(Flying), meta=(BlueprintSpawnableComponent))
class FLYINGPRESENTATION_API UFlyingCesiumGeoreferenceComponent : public UActorComponent
{
  GENERATED_BODY()

public:
  UFlyingCesiumGeoreferenceComponent();

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Cesium")
  TObjectPtr<ACesiumGeoreference> GeoreferenceOverride;

  UFUNCTION(BlueprintCallable, Category="Flying|Cesium")
  ACesiumGeoreference* ResolveGeoreference() const;

  UFUNCTION(BlueprintCallable, Category="Flying|Cesium")
  void ConfigureOriginFromSettings() const;

  UFUNCTION(BlueprintPure, Category="Flying|Cesium")
  FVector TransformEcefPositionToUnreal(const FVector& EcefPositionMeters) const;

  UFUNCTION(BlueprintPure, Category="Flying|Cesium")
  FVector TransformEcefDirectionToUnreal(const FVector& EcefDirection) const;

  UFUNCTION(BlueprintPure, Category="Flying|Cesium")
  FRotator TransformBodyToUnrealRotator(const FFlyingCoreSimStateSnapshot& Snapshot) const;
};
