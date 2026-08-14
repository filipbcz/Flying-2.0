#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "FlyingStartupGameMode.generated.h"

UCLASS()
class FLYINGPRESENTATION_API AFlyingStartupGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    void BeginPlay() override;
};
