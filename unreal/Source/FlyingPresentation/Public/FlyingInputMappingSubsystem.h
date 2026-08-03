#pragma once

#include "CoreMinimal.h"
#include "FlyingInputMappingTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "FlyingInputMappingSubsystem.generated.h"

UCLASS(BlueprintType)
class FLYINGPRESENTATION_API UFlyingInputMappingSubsystem : public UGameInstanceSubsystem
{
  GENERATED_BODY()

public:
  void Initialize(FSubsystemCollectionBase& Collection) override;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Input")
  FFlyingInputSettings InputSettings;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FString InputSettingsPath;

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  void ResetToDefaultInputSettings();

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool LoadInputSettings(TArray<FString>& OutErrors);

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool SaveInputSettings(TArray<FString>& OutErrors) const;

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool ValidateInputSettings(TArray<FString>& OutErrors) const;

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  void SetInputSettingsPath(const FString& NewInputSettingsPath);

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool AddOrReplaceAxisBinding(FName ProfileId, const FFlyingAxisBinding& Binding);

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool SetAxisCalibration(
    FName ProfileId,
    EFlyingFlightControlAxis Axis,
    const FFlyingPhysicalInputBinding& Source,
    const FFlyingAxisCalibration& Calibration);

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool AddOrReplaceCommandBinding(FName ProfileId, const FFlyingCommandBinding& Binding);

  UFUNCTION(BlueprintPure, Category="Flying|Input")
  FFlyingMappedInputState MapRawInputFrame(const TArray<FFlyingRawInputControlValue>& RawControls) const;

private:
  FString ResolveInputSettingsPath() const;
};
