#pragma once

#include "CoreMinimal.h"

#include "FlyingCoreSimStateSnapshot.generated.h"

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

  FQuat BodyToEcef = FQuat::Identity;
};
