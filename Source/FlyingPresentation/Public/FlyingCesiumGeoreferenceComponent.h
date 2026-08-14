#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "FlyingCesiumGeoreferenceComponent.generated.h"

class ACesiumGeoreference;

UCLASS(BlueprintType, ClassGroup=(Flying), meta=(BlueprintSpawnableComponent))
class FLYINGPRESENTATION_API UFlyingCesiumGeoreferenceComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFlyingCesiumGeoreferenceComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Georeference")
    TObjectPtr<ACesiumGeoreference> GeoreferenceOverride;

    UFUNCTION(BlueprintCallable, Category="Flying|Georeference")
    ACesiumGeoreference* ResolveGeoreference() const;

    UFUNCTION(BlueprintCallable, Category="Flying|Georeference")
    void ConfigureStartupOrigin() const;

    UFUNCTION(BlueprintPure, Category="Flying|Georeference")
    FVector TransformEcefPositionToUnreal(const FVector& EcefPositionMeters) const;

    UFUNCTION(BlueprintPure, Category="Flying|Georeference")
    FVector TransformEcefDirectionToUnreal(const FVector& EcefDirection) const;
};
