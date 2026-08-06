#pragma once

#include "CoreMinimal.h"

#include "FlyingCoreSimStateSnapshot.generated.h"

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingWeatherSnapshot
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double StaticPressurePascal = 101325.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double TemperatureKelvin = 288.15;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double DensityKgPerCubicMeter = 1.225;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double RelativeHumidityNorm = 0.5;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  FVector WindNedMetersPerSecond = FVector::ZeroVector;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  FVector TurbulenceNedMetersPerSecond = FVector::ZeroVector;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double VisibilityMeters = 30000.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double CloudCoverageNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double PrecipitationRateMmPerHour = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double SurfaceWetnessNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double IcingSeverityNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double RunwayFrictionScale = 1.0;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingManualWeatherScenario
{
  GENERATED_BODY()

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double QnhPascal = 101325.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double SeaLevelTemperatureKelvin = 288.15;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double RelativeHumidityNorm = 0.5;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double VisibilityMeters = 30000.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  FVector SurfaceWindNedMetersPerSecond = FVector::ZeroVector;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  FVector WindAloftNedMetersPerSecond = FVector::ZeroVector;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double WindAloftAltitudeMeters = 1000.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double TurbulenceIntensityMetersPerSecond = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  int32 TurbulenceSeed = 1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double CloudBaseMeters = 1200.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double CloudTopMeters = 2000.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double CloudCoverageNorm = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double RainRateMmPerHour = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double SnowRateMmPerHour = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double SurfaceWetnessNorm = 0.0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Flying|Weather")
  double IcingSeverityNorm = 0.0;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingElectricalInstrumentSnapshot
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double BusVoltageVolts = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double BatteryChargeNorm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double AlternatorOutputWatts = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bBatteryOnline = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bAlternatorOnline = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bAvionicsBusPowered = false;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingFuelInstrumentSnapshot
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double LeftQuantityKg = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double RightQuantityKg = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double FuelPressureKpa = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double FuelFlowKgPerSecond = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bEngineFuelStarved = false;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingEngineInstrumentSnapshot
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double Rpm = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double ManifoldPressureKpa = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double OilTemperatureKelvin = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double CylinderHeadTemperatureKelvin = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double ExhaustGasTemperatureKelvin = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bValid = false;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingAircraftInstrumentSnapshot
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bValid = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  int64 Sequence = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double IndicatedAirspeedMetersPerSecond = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double IndicatedAltitudeMeters = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double VerticalSpeedMetersPerSecond = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double MagneticHeadingDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double AttitudeRollDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double AttitudePitchDegrees = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  double VacuumSuctionInHg = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bGpsValid = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bPitotBlocked = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  bool bStaticBlocked = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  FFlyingEngineInstrumentSnapshot Engine;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  FFlyingElectricalInstrumentSnapshot Electrical;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Instruments")
  FFlyingFuelInstrumentSnapshot Fuel;
};

USTRUCT(BlueprintType)
struct FLYINGPRESENTATION_API FFlyingCoreSimStateSnapshot
{
  GENERATED_BODY()

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  bool bValid = false;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  double SimulationTimeSeconds = 0.0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  int64 StepIndex = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  int64 StateHash = 0;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  FVector EcefPositionMeters = FVector::ZeroVector;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  FVector EcefVelocityMetersPerSecond = FVector::ZeroVector;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|CoreSim")
  FVector4 BodyToEcefQuaternionXyzw = FVector4(0.0, 0.0, 0.0, 1.0);

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  FFlyingWeatherSnapshot Weather;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  FVector RelativeAirVelocityBodyMetersPerSecond = FVector::ZeroVector;

  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Flying|Weather")
  double WeatherDynamicPressurePascal = 0.0;

  FQuat BodyToEcef = FQuat::Identity;
};
