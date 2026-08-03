#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "FlyingReplayWidget.generated.h"

class UFlyingCoreSimComponent;

UCLASS(BlueprintType, Blueprintable)
class FLYINGPRESENTATION_API UFlyingReplayWidget : public UUserWidget
{
  GENERATED_BODY()

public:
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Replay")
  FString TelemetryPath;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Replay")
  FString ReplayPath;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Replay")
  FString CsvExportPath;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Replay")
  FString JsonExportPath;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Replay")
  FString LastStatus;

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool StartRecording(UFlyingCoreSimComponent* CoreSimComponent, const FString& SessionId);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool StopRecording(UFlyingCoreSimComponent* CoreSimComponent);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool LoadReplay(UFlyingCoreSimComponent* CoreSimComponent, bool bWarnOnIncompatible);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool PlayReplay(UFlyingCoreSimComponent* CoreSimComponent, bool bWarnOnIncompatible);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool ExportCsv(UFlyingCoreSimComponent* CoreSimComponent);

  UFUNCTION(BlueprintCallable, Category="Flying|Replay")
  bool ExportJson(UFlyingCoreSimComponent* CoreSimComponent);

private:
  void SyncStatus(const UFlyingCoreSimComponent* CoreSimComponent);
};
