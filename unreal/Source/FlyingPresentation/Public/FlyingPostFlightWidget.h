#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlyingOfflineNavigationMapWidget.h"
#include "FlyingPostFlightTypes.h"

#include "FlyingPostFlightWidget.generated.h"

class UFlyingCoreSimComponent;

UCLASS(BlueprintType, Blueprintable)
class FLYINGPRESENTATION_API UFlyingPostFlightWidget : public UUserWidget
{
  GENERATED_BODY()

public:
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Post Flight")
  FString ReplayPath = TEXT("Saved/Flying/Replay/last-flight.telemetry.json");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Post Flight")
  FString CsvExportPath = TEXT("Saved/Flying/Exports/last-flight.csv");

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Post Flight")
  FString JsonExportPath = TEXT("Saved/Flying/Exports/last-flight.json");

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  FString LastStatus;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  TArray<FFlyingTelemetryRoutePoint> Route;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  TArray<FFlyingNavigationMapOverlayPoint> RouteMapTrack;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  TArray<FFlyingTelemetryGraphSeries> Graphs;

  UFUNCTION(BlueprintCallable, Category="Flying|Post Flight")
  bool LoadReplay(UFlyingCoreSimComponent* CoreSimComponent, bool bWarnOnIncompatible);

  UFUNCTION(BlueprintCallable, Category="Flying|Post Flight")
  bool PlayReplay(UFlyingCoreSimComponent* CoreSimComponent, bool bWarnOnIncompatible);

  UFUNCTION(BlueprintCallable, Category="Flying|Post Flight")
  bool RefreshPostFlightData(UFlyingCoreSimComponent* CoreSimComponent);

  UFUNCTION(BlueprintCallable, Category="Flying|Post Flight")
  bool ExportCsv(UFlyingCoreSimComponent* CoreSimComponent);

  UFUNCTION(BlueprintCallable, Category="Flying|Post Flight")
  bool ExportJson(UFlyingCoreSimComponent* CoreSimComponent);
};
