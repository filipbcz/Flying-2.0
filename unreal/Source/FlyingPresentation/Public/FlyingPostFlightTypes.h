#pragma once

#include "CoreMinimal.h"

#include "FlyingPostFlightTypes.generated.h"

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingTelemetryRoutePoint
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  double LatitudeDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  double LongitudeDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  double AltitudeMeters = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  double TimeSeconds = 0.0;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingTelemetryGraphPoint
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  double TimeSeconds = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  double Value = 0.0;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingTelemetryGraphSeries
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  FName SeriesId;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  FString DisplayName;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Post Flight")
  TArray<FFlyingTelemetryGraphPoint> Points;
};
