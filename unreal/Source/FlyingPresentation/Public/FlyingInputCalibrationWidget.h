#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlyingInputMappingTypes.h"

#include "FlyingInputCalibrationWidget.generated.h"

UCLASS(BlueprintType, Blueprintable)
class FLYINGPRESENTATION_API UFlyingInputCalibrationWidget : public UUserWidget
{
  GENERATED_BODY()

public:
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FFlyingInputDeviceProfile EditingProfile;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Input")
  FFlyingAxisBinding SelectedAxisBinding;

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool SelectAxisBinding(EFlyingFlightControlAxis Axis, const FFlyingPhysicalInputBinding& Source);

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  void SetSelectedCalibration(const FFlyingAxisCalibration& Calibration);

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool CommitSelectedCalibration();

  UFUNCTION(BlueprintCallable, Category="Flying|Input")
  bool AddOrReplaceAxisBinding(const FFlyingAxisBinding& Binding);
};
