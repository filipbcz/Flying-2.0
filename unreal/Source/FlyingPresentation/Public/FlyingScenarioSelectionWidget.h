#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlyingScenarioTypes.h"

#include "FlyingScenarioSelectionWidget.generated.h"

class UFlyingCoreSimComponent;

UCLASS(BlueprintType, Blueprintable)
class FLYINGPRESENTATION_API UFlyingScenarioSelectionWidget : public UUserWidget
{
  GENERATED_BODY()

public:
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  TArray<FFlyingScenarioLocation> PilotLocations;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FFlyingScenarioSelection SelectedScenario;

  UFUNCTION(BlueprintCallable, Category="Flying|Scenario")
  void RefreshPilotLocations();

  UFUNCTION(BlueprintCallable, Category="Flying|Scenario")
  bool SelectScenario(FName LocationId, EFlyingScenarioStartMode StartMode);

  UFUNCTION(BlueprintCallable, Category="Flying|Scenario")
  bool StartSelectedScenario(UFlyingCoreSimComponent* CoreSimComponent);

protected:
  virtual void NativeConstruct() override;

private:
  void EnsurePilotLocations();
};
