#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "FlyingScenarioTypes.h"
#include "FlyingInstalledPackageLibrary.generated.h"

UCLASS()
class FLYINGPRESENTATION_API UFlyingInstalledPackageLibrary : public UBlueprintFunctionLibrary
{
  GENERATED_BODY()
public:
  UFUNCTION(BlueprintCallable, Category="Flying|Installed Packages")
  static bool SelectInstalledPackage(const FString& DataRoot, const FString& RelativePackage, const FString& ManifestSha256, FString& Diagnostic);
  UFUNCTION(BlueprintCallable, Category="Flying|Installed Packages")
  static TArray<FFlyingScenarioLocation> GetPackageLocations(FString& Diagnostic);
  UFUNCTION(BlueprintCallable, Category="Flying|Installed Packages")
  static bool BindSelection(UPARAM(ref) FFlyingScenarioSelection& Selection, FString& Diagnostic);
};
