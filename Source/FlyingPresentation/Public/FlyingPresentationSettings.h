#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "FlyingPresentationSettings.generated.h"

UCLASS(Config=Engine, DefaultConfig, meta=(DisplayName="Flying Presentation"))
class FLYINGPRESENTATION_API UFlyingPresentationSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category="Flying")
    bool bOfflineOnly = true;

    UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category="Flying")
    FString CesiumForUnrealMinimumVersion = TEXT("2.28.0");

    UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category="Flying|Startup")
    double StartupOriginLongitudeDegrees = 14.42076;

    UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category="Flying|Startup")
    double StartupOriginLatitudeDegrees = 50.08804;

    UPROPERTY(Config, EditDefaultsOnly, BlueprintReadOnly, Category="Flying|Startup")
    double StartupOriginHeightMeters = 250.0;
};
