#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "FlyingPilotGameMode.generated.h"

UCLASS()
class FLYINGPRESENTATION_API AFlyingPilotGameMode : public AGameModeBase
{
  GENERATED_BODY()

public:
  void BeginPlay() override;
};
