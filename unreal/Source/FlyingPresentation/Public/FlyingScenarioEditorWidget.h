#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlyingScenarioTypes.h"

#include "FlyingScenarioEditorWidget.generated.h"

class UFlyingCoreSimComponent;

UCLASS(BlueprintType, Blueprintable)
class FLYINGPRESENTATION_API UFlyingScenarioEditorWidget : public UUserWidget
{
  GENERATED_BODY()

public:
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FFlyingScenarioEditorData EditedScenario;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FString ScenarioSavePath = TEXT("Saved/Flying/Scenarios/current-scenario.json");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Scenario")
  FString SettingsSavePath = TEXT("Saved/Flying/Settings/user-settings.json");

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Scenario")
  FString LastStatus;

  UFUNCTION(BlueprintCallable, Category="Flying|Scenario")
  void AddOrUpdateFailure(FName FailureId, bool bFailed);

  UFUNCTION(BlueprintCallable, Category="Flying|Scenario")
  bool ApplyEditedScenario(UFlyingCoreSimComponent* CoreSimComponent);

  UFUNCTION(BlueprintCallable, Category="Flying|Save")
  bool SaveScenario();

  UFUNCTION(BlueprintCallable, Category="Flying|Save")
  bool LoadScenario();

  UFUNCTION(BlueprintCallable, Category="Flying|Save")
  bool SaveSettings();

  UFUNCTION(BlueprintCallable, Category="Flying|Save")
  bool LoadSettings();
};
