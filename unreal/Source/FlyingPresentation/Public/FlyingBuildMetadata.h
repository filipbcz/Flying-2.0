#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FlyingBuildMetadata.generated.h"

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingBuildMetadata
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Build")
  FString BuildId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Build")
  FString Version;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Build")
  FString Channel;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Build")
  FString Commit;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Build")
  FString BuiltAtUtc;
};

UCLASS()
class FLYINGPRESENTATION_API UFlyingBuildMetadata : public UBlueprintFunctionLibrary
{
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintPure, Category="Flying|Build")
  static FFlyingBuildMetadata GetBuildMetadata();

  UFUNCTION(BlueprintPure, Category="Flying|Build")
  static FString GetBuildId();

  UFUNCTION(BlueprintPure, Category="Flying|Build")
  static FString GetAboutBuildSummary();
};
