#include "FlyingCoreSimComponent.h"

#include "FlyingPresentationSettings.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/aircraft_systems.hpp"
#include "flying/core_sim/scenario.hpp"
#include "flying/core_sim/simulator.hpp"
#include "flying/core_sim/telemetry.hpp"
#include "flying/core_sim/weather.hpp"
#include "flying/geo_terrain/geodesy.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using flying::core_sim::AuthoritativeState;
using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::AircraftSystemsInput;
using flying::core_sim::AircraftSystemsModel;
using flying::core_sim::ControlInputSample;
using flying::core_sim::CoreSimulator;
using flying::core_sim::DataPackageVersion;
using flying::core_sim::AdvanceReport;
using flying::core_sim::Quaterniond;
using flying::core_sim::ReplayEnvironment;
using flying::core_sim::Vector3d;
using flying::core_sim::WeatherSample;
using flying::core_sim::WeatherScenario;
using flying::geo_terrain::EcefPosition;
using flying::geo_terrain::EnuVector;
using flying::geo_terrain::GeodeticCoordinates;
using flying::geo_terrain::LocalTangentFrame;

const TCHAR* const kTerrainPackageSchemaVersion = TEXT("flying.terrain-package.v1");
const TCHAR* const kPilotRegionPackageSchemaVersion = TEXT("flying.pilot-region-package.v1");
const TCHAR* const kDefaultAircraftId = TEXT("flying_trainer_one");

FVector ToUnrealVector(Vector3d Value)
{
  return FVector(Value.x, Value.y, Value.z);
}

FQuat ToUnrealQuat(Quaterniond Value)
{
  const Quaterniond Normalized = Value.normalized();
  return FQuat(Normalized.x, Normalized.y, Normalized.z, Normalized.w);
}

std::string ToStdString(const FString& Value)
{
  return std::string(TCHAR_TO_UTF8(*Value));
}

std::filesystem::path ToPath(const FString& Value)
{
  return std::filesystem::path(ToStdString(Value));
}

double JsonScalarValue(const TSharedPtr<FJsonObject>& Object,
                       const TCHAR* FieldName,
                       double Fallback)
{
  const TSharedPtr<FJsonObject>* Scalar = nullptr;
  if (!Object.IsValid() ||
      !Object->TryGetObjectField(FieldName, Scalar) ||
      !Scalar ||
      !Scalar->IsValid())
  {
    return Fallback;
  }

  double Value = Fallback;
  (*Scalar)->TryGetNumberField(TEXT("value"), Value);
  return Value;
}

double JsonInertiaValue(const TSharedPtr<FJsonObject>& MassBalance,
                        const TCHAR* FieldName,
                        double Fallback)
{
  const TSharedPtr<FJsonObject>* Wrapper = nullptr;
  const TSharedPtr<FJsonObject>* ValueObject = nullptr;
  if (!MassBalance.IsValid() ||
      !MassBalance->TryGetObjectField(TEXT("emptyInertiaKgM2"), Wrapper) ||
      !Wrapper ||
      !Wrapper->IsValid() ||
      !(*Wrapper)->TryGetObjectField(TEXT("value"), ValueObject) ||
      !ValueObject ||
      !ValueObject->IsValid())
  {
    return Fallback;
  }

  double Value = Fallback;
  (*ValueObject)->TryGetNumberField(FieldName, Value);
  return Value;
}

FString ResolveProjectPath(const FString& RawPath)
{
  if (RawPath.IsEmpty())
  {
    return {};
  }

  FString Resolved = RawPath;
  if (FPaths::IsRelative(Resolved))
  {
    Resolved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Resolved);
  }
  FPaths::NormalizeFilename(Resolved);
  FPaths::CollapseRelativeDirectories(Resolved);
  return Resolved;
}

FString ResolveAircraftConfigPath(const std::string& AircraftId)
{
  const FString AircraftIdText = UTF8_TO_TCHAR(AircraftId.c_str());
  const FString RelativePath =
    FString::Printf(TEXT("core_sim/aircraft/%s/aircraft-config.json"), *AircraftIdText);
  const FString ProjectRelativePath =
    FString::Printf(TEXT("../core_sim/aircraft/%s/aircraft-config.json"), *AircraftIdText);

  TArray<FString> Candidates;
  Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ProjectRelativePath));
  Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), RelativePath));
  Candidates.Add(FPaths::ConvertRelativePathToFull(RelativePath));

  for (FString& Candidate : Candidates)
  {
    FPaths::NormalizeFilename(Candidate);
    FPaths::CollapseRelativeDirectories(Candidate);
    if (FPaths::FileExists(Candidate))
    {
      return Candidate;
    }
  }
  return Candidates.Num() > 0 ? Candidates[0] : FString{};
}

struct FAircraftRuntimeConfig
{
  flying::core_sim::RigidBodyParameters Parameters;
  double FuelCapacityKg = 100.0;
  double MaxPayloadKg = 600.0;
};

FAircraftRuntimeConfig LoadAircraftRuntimeConfig(const std::string& AircraftId)
{
  if (AircraftId.empty() ||
      AircraftId.find('/') != std::string::npos ||
      AircraftId.find('\\') != std::string::npos ||
      AircraftId.find("..") != std::string::npos ||
      AircraftId.find(':') != std::string::npos)
  {
    throw std::runtime_error("Aircraft id contains invalid path characters");
  }

  const FString ConfigPath = ResolveAircraftConfigPath(AircraftId);
  FString Json;
  if (ConfigPath.IsEmpty() || !FFileHelper::LoadFileToString(Json, *ConfigPath))
  {
    throw std::runtime_error("Aircraft configuration could not be loaded: " + AircraftId);
  }

  TSharedPtr<FJsonObject> Root;
  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
  if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
  {
    throw std::runtime_error("Aircraft configuration is corrupt: " + AircraftId);
  }

  FString SchemaVersion;
  if (!Root->TryGetStringField(TEXT("schemaVersion"), SchemaVersion) ||
      SchemaVersion != TEXT("flying.aircraft-config.v1"))
  {
    throw std::runtime_error("Aircraft configuration schema is unsupported: " + AircraftId);
  }

  const TSharedPtr<FJsonObject>* MassBalance = nullptr;
  if (!Root->TryGetObjectField(TEXT("massBalance"), MassBalance) ||
      !MassBalance ||
      !MassBalance->IsValid())
  {
    throw std::runtime_error("Aircraft configuration is missing massBalance: " + AircraftId);
  }

  FAircraftRuntimeConfig Config;
  Config.Parameters.mass_kg =
    JsonScalarValue(*MassBalance, TEXT("emptyMassKg"), Config.Parameters.mass_kg);
  Config.Parameters.inertia_diagonal_kg_m2 = {
    JsonInertiaValue(*MassBalance, TEXT("ixx"), Config.Parameters.inertia_diagonal_kg_m2.x),
    JsonInertiaValue(*MassBalance, TEXT("iyy"), Config.Parameters.inertia_diagonal_kg_m2.y),
    JsonInertiaValue(*MassBalance, TEXT("izz"), Config.Parameters.inertia_diagonal_kg_m2.z)};

  Config.FuelCapacityKg = 0.0;
  const TArray<TSharedPtr<FJsonValue>>* FuelStations = nullptr;
  if ((*MassBalance)->TryGetArrayField(TEXT("fuelStations"), FuelStations) && FuelStations)
  {
    for (const TSharedPtr<FJsonValue>& StationValue : *FuelStations)
    {
      Config.FuelCapacityKg +=
        JsonScalarValue(StationValue.IsValid() ? StationValue->AsObject() : nullptr,
                        TEXT("capacityKg"),
                        0.0);
    }
  }
  if (Config.FuelCapacityKg <= 0.0)
  {
    Config.FuelCapacityKg = 100.0;
  }

  Config.MaxPayloadKg = 0.0;
  const TArray<TSharedPtr<FJsonValue>>* PayloadStations = nullptr;
  if ((*MassBalance)->TryGetArrayField(TEXT("payloadStations"), PayloadStations) && PayloadStations)
  {
    for (const TSharedPtr<FJsonValue>& StationValue : *PayloadStations)
    {
      Config.MaxPayloadKg +=
        JsonScalarValue(StationValue.IsValid() ? StationValue->AsObject() : nullptr,
                        TEXT("maxMassKg"),
                        0.0);
    }
  }
  if (Config.MaxPayloadKg <= 0.0)
  {
    Config.MaxPayloadKg = 600.0;
  }

  return Config;
}

std::string ToStdString(FName Value)
{
  return ToStdString(Value.ToString());
}

FString ToFString(const std::string& Value)
{
  return FString(UTF8_TO_TCHAR(Value.c_str()));
}

FName ToFName(const std::string& Value)
{
  return FName(*ToFString(Value));
}

double WrapDegrees(double Value)
{
  double Wrapped = std::fmod(Value, 360.0);
  if (Wrapped < 0.0)
  {
    Wrapped += 360.0;
  }
  return Wrapped;
}

double EstimateEngineRpm(const AircraftControlInputSample& Controls, bool bEngineRunning)
{
  if (!bEngineRunning || Controls.mixture_norm <= 0.02)
  {
    return 0.0;
  }

  const double Throttle = FMath::Clamp(Controls.throttle_norm, 0.0, 1.0);
  const double Propeller = FMath::Clamp(Controls.propeller_norm, 0.35, 1.0);
  return (650.0 + Throttle * 2050.0) * Propeller;
}

flying::core_sim::FlightDynamicsState MakeSystemsTruth(const AuthoritativeState& State)
{
  flying::core_sim::FlightDynamicsState Truth;
  Truth.simulation_time_s = State.simulation_time_s;
  Truth.step_index = State.step_index;
  Truth.ecef_position_m = State.ecef_position_m;
  Truth.ecef_velocity_mps = State.ecef_velocity_mps;
  Truth.body_to_ecef = State.body_to_ecef;
  Truth.angular_velocity_body_radps = State.angular_velocity_body_radps;
  Truth.total_force_body_n = State.accumulated_force_body_n;
  Truth.total_moment_body_nm = State.accumulated_moment_body_nm;

  const EcefPosition Ecef{
    {State.ecef_position_m.x, State.ecef_position_m.y, State.ecef_position_m.z}};
  const GeodeticCoordinates Geodetic = flying::geo_terrain::ecef_to_geodetic(Ecef);
  Truth.latitude_deg = Geodetic.latitude_degrees();
  Truth.longitude_deg = Geodetic.longitude_degrees();
  Truth.altitude_m = Geodetic.ellipsoidal_height.meters;

  const LocalTangentFrame Frame = flying::geo_terrain::make_local_tangent_frame(Geodetic);
  const flying::geo_terrain::NedVector Ned =
    flying::geo_terrain::ned_from_ecef_vector(
      Frame,
      flying::geo_terrain::EcefVector{
        {State.ecef_velocity_mps.x, State.ecef_velocity_mps.y, State.ecef_velocity_mps.z}});
  Truth.ned_velocity_mps = {Ned.north_m, Ned.east_m, Ned.down_m};

  const Quaterniond BodyToEcef = State.body_to_ecef.normalized();
  const Quaterniond EcefToBody = BodyToEcef.conjugated();
  const Vector3d BodyVelocity = EcefToBody.rotate(State.ecef_velocity_mps);
  Truth.body_velocity_mps = BodyVelocity;

  const FQuat UnrealBodyToEcef(BodyToEcef.x, BodyToEcef.y, BodyToEcef.z, BodyToEcef.w);
  const FRotator BodyRotator = UnrealBodyToEcef.Rotator();
  Truth.euler_rad = {
    FMath::DegreesToRadians(BodyRotator.Roll),
    FMath::DegreesToRadians(BodyRotator.Pitch),
    FMath::DegreesToRadians(BodyRotator.Yaw)};
  Truth.calibrated_airspeed_mps =
    std::sqrt(std::max(0.0, BodyVelocity.x * BodyVelocity.x + BodyVelocity.y * BodyVelocity.y));
  return Truth;
}

FFlyingAircraftInstrumentSnapshot ToUnreal(
  const flying::core_sim::InstrumentData& Instruments)
{
  FFlyingAircraftInstrumentSnapshot Result;
  Result.bValid = true;
  Result.Sequence = static_cast<int64>(Instruments.sequence);
  Result.IndicatedAirspeedMetersPerSecond = Instruments.indicated_airspeed_mps;
  Result.IndicatedAltitudeMeters = Instruments.indicated_altitude_m;
  Result.VerticalSpeedMetersPerSecond = Instruments.vertical_speed_mps;
  Result.MagneticHeadingDegrees =
    WrapDegrees(FMath::RadiansToDegrees(Instruments.magnetic_heading_rad));
  Result.AttitudeRollDegrees = FMath::RadiansToDegrees(Instruments.attitude_roll_rad);
  Result.AttitudePitchDegrees = FMath::RadiansToDegrees(Instruments.attitude_pitch_rad);
  Result.VacuumSuctionInHg = Instruments.vacuum.suction_inhg;
  Result.bGpsValid = Instruments.gps.valid;
  Result.bPitotBlocked = Instruments.pitot_static.pitot_blocked;
  Result.bStaticBlocked = Instruments.pitot_static.static_blocked;

  Result.Engine.Rpm = Instruments.engine.rpm;
  Result.Engine.ManifoldPressureKpa = Instruments.engine.manifold_pressure_kpa;
  Result.Engine.OilTemperatureKelvin = Instruments.engine.oil_temperature_k;
  Result.Engine.CylinderHeadTemperatureKelvin = Instruments.engine.cylinder_head_temperature_k;
  Result.Engine.ExhaustGasTemperatureKelvin = Instruments.engine.exhaust_gas_temperature_k;
  Result.Engine.bValid = Instruments.engine.valid;

  Result.Electrical.BusVoltageVolts = Instruments.electrical.bus_voltage_v;
  Result.Electrical.BatteryChargeNorm = Instruments.electrical.battery_charge_norm;
  Result.Electrical.AlternatorOutputWatts = Instruments.electrical.alternator_output_w;
  Result.Electrical.bBatteryOnline = Instruments.electrical.battery_online;
  Result.Electrical.bAlternatorOnline = Instruments.electrical.alternator_online;
  Result.Electrical.bAvionicsBusPowered = Instruments.electrical.avionics_bus_powered;

  Result.Fuel.LeftQuantityKg = Instruments.fuel.tanks.left_quantity_kg;
  Result.Fuel.RightQuantityKg = Instruments.fuel.tanks.right_quantity_kg;
  Result.Fuel.FuelPressureKpa = Instruments.fuel.fuel_pressure_kpa;
  Result.Fuel.FuelFlowKgPerSecond = Instruments.engine.fuel_flow_kgps;
  Result.Fuel.bEngineFuelStarved = Instruments.fuel.engine_fuel_starved;
  return Result;
}

FFlyingWeatherSnapshot ToUnreal(const WeatherSample& Value)
{
  FFlyingWeatherSnapshot Result;
  Result.StaticPressurePascal = Value.atmosphere.static_pressure_pa;
  Result.TemperatureKelvin = Value.atmosphere.temperature_k;
  Result.DensityKgPerCubicMeter = Value.atmosphere.density_kgpm3;
  Result.RelativeHumidityNorm = Value.atmosphere.relative_humidity_norm;
  Result.WindNedMetersPerSecond = ToUnrealVector(Value.wind_ned_mps);
  Result.TurbulenceNedMetersPerSecond = ToUnrealVector(Value.turbulence_ned_mps);
  Result.VisibilityMeters = Value.visibility_m;
  Result.CloudCoverageNorm = Value.cloud_coverage_norm;
  Result.PrecipitationRateMmPerHour = Value.precipitation_rate_mmph;
  Result.SurfaceWetnessNorm = Value.surface_wetness_norm;
  Result.IcingSeverityNorm = Value.icing_severity_norm;
  Result.RunwayFrictionScale = Value.runway_friction_scale;
  return Result;
}

WeatherScenario ToCore(const FFlyingManualWeatherScenario& Value)
{
  WeatherScenario Result;
  Result.scenario_id = "unreal.manual";
  Result.qnh_pa = Value.QnhPascal;
  Result.sea_level_temperature_k = Value.SeaLevelTemperatureKelvin;
  Result.relative_humidity_norm = FMath::Clamp(Value.RelativeHumidityNorm, 0.0, 1.0);
  Result.visibility_m = FMath::Max(0.0, Value.VisibilityMeters);
  Result.surface_wind.wind_ned_mps = {
    Value.SurfaceWindNedMetersPerSecond.X,
    Value.SurfaceWindNedMetersPerSecond.Y,
    Value.SurfaceWindNedMetersPerSecond.Z};
  Result.wind_aloft.altitude_m = Value.WindAloftAltitudeMeters;
  Result.wind_aloft.wind_ned_mps = {
    Value.WindAloftNedMetersPerSecond.X,
    Value.WindAloftNedMetersPerSecond.Y,
    Value.WindAloftNedMetersPerSecond.Z};
  Result.turbulence.intensity_mps = FMath::Max(0.0, Value.TurbulenceIntensityMetersPerSecond);
  Result.turbulence.seed = static_cast<uint32>(FMath::Max(1, Value.TurbulenceSeed));
  Result.cloud.base_altitude_m = Value.CloudBaseMeters;
  Result.cloud.top_altitude_m = Value.CloudTopMeters;
  Result.cloud.coverage_norm = FMath::Clamp(Value.CloudCoverageNorm, 0.0, 1.0);
  Result.precipitation.rain_rate_mmph = FMath::Max(0.0, Value.RainRateMmPerHour);
  Result.precipitation.snow_rate_mmph = FMath::Max(0.0, Value.SnowRateMmPerHour);
  Result.precipitation.surface_wetness_norm = FMath::Clamp(Value.SurfaceWetnessNorm, 0.0, 1.0);
  Result.icing_severity_norm = FMath::Clamp(Value.IcingSeverityNorm, 0.0, 1.0);
  return Result;
}

std::string MakePackageVersionError(
  const TCHAR* PackageLabel,
  const FString& ResolvedPath,
  const TCHAR* Reason)
{
  return ToStdString(FString::Printf(
    TEXT("%s data package version unavailable at %s: %s"),
    PackageLabel,
    *ResolvedPath,
    Reason));
}

bool TryAppendManifestDataPackage(
  const FString& RawPath,
  const TCHAR* ExpectedSchemaVersion,
  const TCHAR* PackageLabel,
  std::vector<DataPackageVersion>& OutPackages,
  std::string& OutError)
{
  const FString ResolvedPath = ResolveProjectPath(RawPath);
  if (ResolvedPath.IsEmpty())
  {
    OutError = ToStdString(FString::Printf(
      TEXT("%s data package manifest path is empty"),
      PackageLabel));
    return false;
  }

  FString JsonText;
  if (!FFileHelper::LoadFileToString(JsonText, *ResolvedPath))
  {
    OutError = MakePackageVersionError(PackageLabel, ResolvedPath, TEXT("manifest could not be read"));
    return false;
  }

  TSharedPtr<FJsonObject> Manifest;
  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
  if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
  {
    OutError = MakePackageVersionError(PackageLabel, ResolvedPath, TEXT("manifest could not be parsed"));
    return false;
  }

  FString SchemaVersion;
  if (!Manifest->TryGetStringField(TEXT("schemaVersion"), SchemaVersion) ||
      SchemaVersion != ExpectedSchemaVersion)
  {
    OutError = MakePackageVersionError(
      PackageLabel,
      ResolvedPath,
      TEXT("manifest schemaVersion is unsupported"));
    return false;
  }

  FString PackageId;
  FString PackageVersion;
  if (!Manifest->TryGetStringField(TEXT("packageId"), PackageId) ||
      !Manifest->TryGetStringField(TEXT("packageVersion"), PackageVersion) ||
      PackageId.IsEmpty() ||
      PackageVersion.IsEmpty())
  {
    OutError = MakePackageVersionError(
      PackageLabel,
      ResolvedPath,
      TEXT("manifest packageId/packageVersion is missing"));
    return false;
  }

  OutPackages.push_back({ToStdString(PackageId), ToStdString(PackageVersion)});
  return true;
}

bool TryMakeUnrealDataPackages(
  std::vector<DataPackageVersion>& OutPackages,
  std::string& OutError)
{
  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  OutPackages.clear();
  return TryAppendManifestDataPackage(
           Settings->TerrainPackageManifestPath,
           kTerrainPackageSchemaVersion,
           TEXT("Terrain"),
           OutPackages,
           OutError) &&
         TryAppendManifestDataPackage(
           Settings->PilotRegionPackageManifestPath,
           kPilotRegionPackageSchemaVersion,
           TEXT("Pilot region"),
           OutPackages,
           OutError);
}

void AppendReplayEnvironmentErrors(
  flying::core_sim::ReplayCompatibilityResult& Compatibility,
  const std::vector<std::string>& EnvironmentErrors)
{
  if (EnvironmentErrors.empty())
  {
    return;
  }

  Compatibility.compatible = false;
  Compatibility.errors.insert(
    Compatibility.errors.end(),
    EnvironmentErrors.begin(),
    EnvironmentErrors.end());
  Compatibility.warnings = Compatibility.errors;
}

ReplayEnvironment MakeUnrealReplayEnvironment(
  const flying::core_sim::RigidBodyParameters& Parameters,
  std::vector<std::string>& OutEnvironmentErrors)
{
  ReplayEnvironment Environment = flying::core_sim::make_current_replay_environment();
  Environment.rigid_body_parameters = Parameters;

  std::vector<DataPackageVersion> Packages;
  std::string PackageError;
  if (TryMakeUnrealDataPackages(Packages, PackageError))
  {
    Environment.data_packages = std::move(Packages);
  }
  else
  {
    Environment.data_packages.clear();
    OutEnvironmentErrors.push_back(PackageError);
  }

  return Environment;
}

ControlInputSample ToCoreInput(const FVector& ForceBodyNewtons,
                               const FVector& MomentBodyNewtonMeters)
{
  return {
    {ForceBodyNewtons.X, ForceBodyNewtons.Y, ForceBodyNewtons.Z},
    {MomentBodyNewtonMeters.X, MomentBodyNewtonMeters.Y, MomentBodyNewtonMeters.Z},
  };
}

AircraftControlInputSample ToCoreAircraftControls(const FFlyingMappedInputState& Value)
{
  AircraftControlInputSample Controls;
  Controls.aileron_norm = Value.RollNorm;
  Controls.elevator_norm = Value.PitchNorm;
  Controls.rudder_norm = Value.YawNorm;
  Controls.throttle_norm = Value.ThrottleNorm;
  Controls.flaps_norm = Value.FlapsNorm;
  Controls.brake_left_norm = Value.BrakeLeftNorm;
  Controls.brake_right_norm = Value.BrakeRightNorm;
  Controls.mixture_norm = Value.MixtureNorm;
  Controls.propeller_norm = Value.PropellerNorm;
  Controls.elevator_trim_norm = Value.ElevatorTrimNorm;
  Controls.aileron_trim_norm = Value.AileronTrimNorm;
  Controls.rudder_trim_norm = Value.RudderTrimNorm;
  return Controls;
}

flying::core_sim::ScenarioStartMode ToCore(EFlyingScenarioStartMode Value)
{
  switch (Value)
  {
  case EFlyingScenarioStartMode::ColdAndDark:
    return flying::core_sim::ScenarioStartMode::ColdAndDark;
  case EFlyingScenarioStartMode::ReadyToTaxi:
    return flying::core_sim::ScenarioStartMode::ReadyToTaxi;
  case EFlyingScenarioStartMode::Airborne:
    return flying::core_sim::ScenarioStartMode::Airborne;
  }

  return flying::core_sim::ScenarioStartMode::ReadyToTaxi;
}

EFlyingScenarioStartMode ToUnreal(flying::core_sim::ScenarioStartMode Value)
{
  switch (Value)
  {
  case flying::core_sim::ScenarioStartMode::ColdAndDark:
    return EFlyingScenarioStartMode::ColdAndDark;
  case flying::core_sim::ScenarioStartMode::ReadyToTaxi:
    return EFlyingScenarioStartMode::ReadyToTaxi;
  case flying::core_sim::ScenarioStartMode::Airborne:
    return EFlyingScenarioStartMode::Airborne;
  }

  return EFlyingScenarioStartMode::ReadyToTaxi;
}

flying::core_sim::ScenarioSelection ToCore(const FFlyingScenarioSelection& Value)
{
  return {ToStdString(Value.LocationId), ToCore(Value.StartMode)};
}

FFlyingScenarioLocation ToUnreal(const flying::core_sim::PilotScenarioLocation& Value)
{
  FFlyingScenarioLocation Result;
  Result.LocationId = ToFName(Value.location_id);
  Result.AerodromeId = ToFString(Value.aerodrome_id);
  Result.RunwayEndId = ToFString(Value.runway_end_id);
  Result.DisplayName = ToFString(Value.display_name);
  Result.LatitudeDegrees = Value.latitude_deg;
  Result.LongitudeDegrees = Value.longitude_deg;
  Result.ElevationMeters = Value.elevation_m;
  Result.TrueHeadingDegrees = Value.true_heading_deg;
  Result.bSelectable = Value.selectable;
  return Result;
}

FFlyingScenarioSelection ToUnreal(const flying::core_sim::ScenarioSelection& Value)
{
  FFlyingScenarioSelection Result;
  Result.LocationId = ToFName(Value.location_id);
  Result.StartMode = ToUnreal(Value.start_mode);
  return Result;
}

FFlyingScenarioRuntimeState ToUnreal(const flying::core_sim::ScenarioInitialState& Value)
{
  FFlyingScenarioRuntimeState Result;
  Result.Selection = ToUnreal(Value.selection);
  Result.Location = ToUnreal(Value.location);
  Result.bBatteryOn = Value.battery_on;
  Result.bEngineRunning = Value.engine_running;
  Result.bAvionicsOn = Value.avionics_on;
  Result.bParkingBrakeSet = Value.parking_brake_set;
  return Result;
}

GeodeticCoordinates MakeGeodeticDegrees(double LatitudeDegrees,
                                        double LongitudeDegrees,
                                        double HeightMeters)
{
  return flying::geo_terrain::make_geodetic_degrees(
    LatitudeDegrees,
    LongitudeDegrees,
    flying::geo_terrain::EllipsoidalHeight{HeightMeters});
}

AuthoritativeState MakeInitialState(double LatitudeDegrees,
                                    double LongitudeDegrees,
                                    double HeightMeters,
                                    const FVector& InitialVelocityEnuMetersPerSecond)
{
  const GeodeticCoordinates Geodetic =
    MakeGeodeticDegrees(LatitudeDegrees, LongitudeDegrees, HeightMeters);
  const EcefPosition Ecef = flying::geo_terrain::geodetic_to_ecef(Geodetic);
  const LocalTangentFrame Frame = flying::geo_terrain::make_local_tangent_frame(Geodetic);
  const auto EcefVelocity =
    flying::geo_terrain::ecef_vector_from_enu(
      Frame,
      EnuVector{
        InitialVelocityEnuMetersPerSecond.X,
        InitialVelocityEnuMetersPerSecond.Y,
        InitialVelocityEnuMetersPerSecond.Z});

  AuthoritativeState State;
  State.ecef_position_m = {Ecef.meters.x, Ecef.meters.y, Ecef.meters.z};
  State.ecef_velocity_mps = {
    EcefVelocity.meters.x,
    EcefVelocity.meters.y,
    EcefVelocity.meters.z};
  State.body_to_ecef = Quaterniond::identity();
  return State;
}

} // namespace

struct FFlyingCoreSimBridgeImpl
{
  CoreSimulator Simulator;
  AircraftSystemsModel Systems;
  AircraftControlInputSample LastAircraftControls;
  std::string ActiveAircraftId = "flying_trainer_one";
  std::string LoadedAircraftId;
  flying::core_sim::RigidBodyParameters ActiveAircraftParameters;
  double ActiveFuelCapacityKg = 100.0;
  double ActiveMaxPayloadKg = 600.0;
  double RequestedFuelMassKg = 80.0;
  double RequestedPayloadMassKg = 180.0;
  bool bLastEngineRunning = false;
  std::optional<flying::core_sim::TelemetryRecorder> Recorder;
  flying::core_sim::TelemetryRecording StoredRecording;
  bool bHasStoredRecording = false;
  std::filesystem::path ActiveTelemetryPath;
  std::string LastStatus;

  void EnsureSelectedAircraftLoaded()
  {
    if (LoadedAircraftId == ActiveAircraftId)
    {
      return;
    }

    const WeatherScenario CurrentWeather = Simulator.weather_scenario();
    const FAircraftRuntimeConfig AircraftConfig = LoadAircraftRuntimeConfig(ActiveAircraftId);
    ActiveAircraftParameters = AircraftConfig.Parameters;
    ActiveFuelCapacityKg = AircraftConfig.FuelCapacityKg;
    ActiveMaxPayloadKg = AircraftConfig.MaxPayloadKg;
    RequestedFuelMassKg = std::clamp(RequestedFuelMassKg, 0.0, ActiveFuelCapacityKg);
    RequestedPayloadMassKg = std::clamp(RequestedPayloadMassKg, 0.0, ActiveMaxPayloadKg);
    LoadedAircraftId = ActiveAircraftId;
    Simulator = CoreSimulator{ActiveAircraftParameters};
    Simulator.set_manual_weather_scenario(CurrentWeather);
  }

  void RecreateSimulatorForScenarioStart()
  {
    EnsureSelectedAircraftLoaded();
    const WeatherScenario CurrentWeather = Simulator.weather_scenario();
    Simulator = CoreSimulator{ActiveAircraftParameters};
    Simulator.set_manual_weather_scenario(CurrentWeather);
  }

  void ApplyRequestedLoadout()
  {
    ApplyMassBalance(RequestedFuelMassKg, RequestedPayloadMassKg);
  }

  void Reset(double LatitudeDegrees,
             double LongitudeDegrees,
             double HeightMeters,
             const FVector& InitialVelocityEnuMetersPerSecond)
  {
    Recorder.reset();
    RecreateSimulatorForScenarioStart();
    Simulator.reset(MakeInitialState(
      LatitudeDegrees,
      LongitudeDegrees,
      HeightMeters,
      InitialVelocityEnuMetersPerSecond));
    ApplyRequestedLoadout();
    Systems.reset();
    LastAircraftControls = Simulator.initial_aircraft_controls();
    bLastEngineRunning = LastAircraftControls.mixture_norm > 0.0;
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
    LastStatus = "CoreSim reset for aircraft " + ActiveAircraftId +
                 "; active telemetry recording stopped";
  }

  AdvanceReport Advance(double DeltaSeconds)
  {
    return Advance(
      DeltaSeconds,
      ControlInputSample{},
      Simulator.initial_aircraft_controls(),
      Simulator.initial_aircraft_controls().mixture_norm > 0.0);
  }

  AdvanceReport Advance(double DeltaSeconds,
                        const ControlInputSample& Input,
                        const AircraftControlInputSample& AircraftControls,
                        bool bEngineRunning)
  {
    const auto Report = Simulator.advance(DeltaSeconds, Input);
    LastAircraftControls = AircraftControls;
    bLastEngineRunning = bEngineRunning;
    StepSystems(DeltaSeconds, AircraftControls, bEngineRunning);
    if (Recorder)
    {
      const flying::core_sim::EngineStateSample Engine =
        flying::core_sim::make_engine_state_sample(AircraftControls, bEngineRunning);
      Recorder->record_advance(
        DeltaSeconds,
        Input,
        AircraftControls,
        Engine,
        Report,
        Simulator.state(),
        flying::core_sim::current_unix_time_ms());
    }
    return Report;
  }

  void ApplyAircraftControls(const AircraftControlInputSample& AircraftControls,
                             bool bEngineRunning)
  {
    LastAircraftControls = AircraftControls;
    bLastEngineRunning = bEngineRunning;
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
  }

  bool ScrubReplay(double PositionNorm)
  {
    if (!bHasStoredRecording || StoredRecording.frames.empty())
    {
      LastStatus = "No telemetry replay frames are loaded";
      return false;
    }

    const double ClampedPosition = std::clamp(PositionNorm, 0.0, 1.0);
    const std::size_t FrameIndex = static_cast<std::size_t>(std::llround(
      ClampedPosition * static_cast<double>(StoredRecording.frames.size() - 1)));
    const flying::core_sim::TelemetryFrame& Frame = StoredRecording.frames[FrameIndex];

    Simulator.reset(
      Frame.state,
      StoredRecording.initial_flight_dynamics,
      Frame.aircraft_controls);
    Systems.reset();
    LastAircraftControls = Frame.aircraft_controls;
    bLastEngineRunning = Frame.engine.engine_running;
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
    LastStatus = "Telemetry replay scrubbed to frame " + std::to_string(Frame.frame_index);
    return true;
  }

  flying::core_sim::ScenarioInitialState ResetScenario(
    const flying::core_sim::ScenarioSelection& Selection)
  {
    Recorder.reset();
    RecreateSimulatorForScenarioStart();
    flying::core_sim::ScenarioInitialState State =
      flying::core_sim::reset_simulator_to_scenario(Simulator, Selection);
    ApplyRequestedLoadout();
    Systems.reset();
    LastAircraftControls = Simulator.initial_aircraft_controls();
    bLastEngineRunning = State.engine_running;
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
    LastStatus = "Scenario started for aircraft " + ActiveAircraftId +
                 "; active telemetry recording stopped";
    return State;
  }

  flying::core_sim::ScenarioInitialState ResetScenarioAtPosition(
    double LatitudeDegrees,
    double LongitudeDegrees,
    double AltitudeMeters,
    double TrueHeadingDegrees,
    flying::core_sim::ScenarioStartMode StartMode)
  {
    Recorder.reset();
    RecreateSimulatorForScenarioStart();
    const double HeightOffsetMeters =
      StartMode == flying::core_sim::ScenarioStartMode::Airborne ? 450.0 : 1.5;
    flying::core_sim::PilotScenarioLocation Location;
    Location.location_id = "custom-position";
    Location.display_name = "Custom geographic position";
    Location.latitude_deg = LatitudeDegrees;
    Location.longitude_deg = LongitudeDegrees;
    Location.elevation_m = AltitudeMeters - HeightOffsetMeters;
    Location.true_heading_deg = TrueHeadingDegrees;
    Location.selectable = true;

    const flying::core_sim::ScenarioSelection Selection{Location.location_id, StartMode};
    const flying::core_sim::PilotScenarioLocation Locations[] = {Location};
    flying::core_sim::ScenarioInitialState State =
      flying::core_sim::reset_simulator_to_scenario(Simulator, Selection, Locations);
    ApplyRequestedLoadout();
    Systems.reset();
    LastAircraftControls = Simulator.initial_aircraft_controls();
    bLastEngineRunning = State.engine_running;
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
    LastStatus = "Custom-position scenario started for aircraft " + ActiveAircraftId +
                 "; active telemetry recording stopped";
    return State;
  }

  bool StartRecording(const std::filesystem::path& OutputPath,
                      const std::string& SessionId,
                      const FFlyingScenarioRuntimeState& ScenarioState)
  {
    if (Recorder)
    {
      LastStatus = "Telemetry recording is already active";
      return false;
    }

    std::vector<DataPackageVersion> DataPackages;
    std::string PackageError;
    if (!TryMakeUnrealDataPackages(DataPackages, PackageError))
    {
      LastStatus = PackageError;
      return false;
    }

    flying::core_sim::TelemetryMetadata Metadata =
      flying::core_sim::make_default_telemetry_metadata();
    Metadata.session_id = SessionId.empty() ? "unreal-core-sim" : SessionId;
    Metadata.simulation_configuration_id = "unreal.core-sim-component.v1";
    Metadata.input_profile_id = "unreal.input-mapping.v1";
    Metadata.scenario_location_id = ToStdString(ScenarioState.Selection.LocationId);
    Metadata.scenario_start_mode =
      std::string(flying::core_sim::to_string(ToCore(ScenarioState.Selection.StartMode)));
    Metadata.data_packages = std::move(DataPackages);

    Recorder.emplace(
      Metadata,
      Simulator.state(),
      Simulator.flight_dynamics_initial_condition(),
      Simulator.initial_aircraft_controls(),
      Simulator.parameters());
    ActiveTelemetryPath = OutputPath;
    LastStatus = "Telemetry recording started";
    return true;
  }

  bool StopRecording(bool bSaveToActivePath)
  {
    if (!Recorder)
    {
      LastStatus = "No active telemetry recording";
      return false;
    }

    StoredRecording = Recorder->recording();
    bHasStoredRecording = true;
    Recorder.reset();

    if (bSaveToActivePath && !ActiveTelemetryPath.empty())
    {
      return SaveRecording(ActiveTelemetryPath);
    }

    LastStatus = "Telemetry recording stopped";
    return true;
  }

  bool SaveRecording(const std::filesystem::path& OutputPath)
  {
    if (OutputPath.empty())
    {
      LastStatus = "Telemetry output path is empty";
      return false;
    }
    if (Recorder)
    {
      StoredRecording = Recorder->recording();
      bHasStoredRecording = true;
    }
    if (!bHasStoredRecording)
    {
      LastStatus = "No telemetry recording is available to save";
      return false;
    }

    const auto Saved = flying::core_sim::save_telemetry_file_atomic(OutputPath, StoredRecording);
    if (!Saved.saved)
    {
      LastStatus = Saved.errors.empty() ? "Telemetry save failed" : Saved.errors.front();
      return false;
    }

    LastStatus = "Telemetry saved";
    return true;
  }

  bool LoadReplay(const std::filesystem::path& InputPath,
                  flying::core_sim::ReplayCompatibilityPolicy Policy)
  {
    if (InputPath.empty())
    {
      LastStatus = "Telemetry replay path is empty";
      return false;
    }
    const auto Loaded = flying::core_sim::load_telemetry_file(InputPath);
    if (!Loaded.loaded)
    {
      LastStatus = Loaded.errors.empty() ? "Telemetry replay load failed" : Loaded.errors.front();
      bHasStoredRecording = false;
      return false;
    }

    std::vector<std::string> EnvironmentErrors;
    ReplayEnvironment Environment =
      MakeUnrealReplayEnvironment(Loaded.recording.rigid_body_parameters, EnvironmentErrors);
    auto Compatibility =
      flying::core_sim::check_replay_compatibility(Loaded.recording.metadata, Environment);
    AppendReplayEnvironmentErrors(Compatibility, EnvironmentErrors);
    if (!Compatibility.compatible &&
        Policy == flying::core_sim::ReplayCompatibilityPolicy::RefuseOnMismatch)
    {
      LastStatus = Compatibility.errors.empty()
                     ? "Telemetry replay is incompatible"
                     : Compatibility.errors.front();
      bHasStoredRecording = false;
      return false;
    }

    StoredRecording = Loaded.recording;
    bHasStoredRecording = true;
    LastStatus = Compatibility.compatible
                   ? "Telemetry replay loaded"
                   : "Telemetry replay loaded with compatibility warnings";
    return true;
  }

  bool PlayReplay(flying::core_sim::ReplayCompatibilityPolicy Policy)
  {
    if (!bHasStoredRecording)
    {
      LastStatus = "No telemetry replay is loaded";
      return false;
    }

    std::vector<std::string> EnvironmentErrors;
    ReplayEnvironment Environment =
      MakeUnrealReplayEnvironment(StoredRecording.rigid_body_parameters, EnvironmentErrors);
    if (!EnvironmentErrors.empty() &&
        Policy == flying::core_sim::ReplayCompatibilityPolicy::RefuseOnMismatch)
    {
      LastStatus = EnvironmentErrors.front();
      return false;
    }

    CoreSimulator ReplaySimulator{StoredRecording.rigid_body_parameters};
    const auto Replay = flying::core_sim::replay_recording(
      StoredRecording,
      ReplaySimulator,
      Environment,
      Policy);
    if (Replay.refused || !Replay.errors.empty())
    {
      LastStatus = Replay.errors.empty() ? "Telemetry replay refused" : Replay.errors.front();
      return false;
    }
    if (!Replay.deterministic)
    {
      LastStatus = Replay.mismatches.empty()
                     ? "Telemetry replay was not deterministic"
                     : Replay.mismatches.front().reason;
      return false;
    }

    Simulator = ReplaySimulator;
    Systems.reset();
    LastAircraftControls = Simulator.initial_aircraft_controls();
    bLastEngineRunning = LastAircraftControls.mixture_norm > 0.0;
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
    LastStatus = Replay.warnings.empty()
                   ? "Telemetry replay reproduced recorded state hashes"
                   : "Telemetry replay reproduced recorded state hashes with compatibility warnings";
    return true;
  }

  bool ExportCsv(const std::filesystem::path& OutputPath)
  {
    if (OutputPath.empty())
    {
      LastStatus = "Telemetry CSV export path is empty";
      return false;
    }
    if (Recorder)
    {
      StoredRecording = Recorder->recording();
      bHasStoredRecording = true;
    }
    if (!bHasStoredRecording)
    {
      LastStatus = "No telemetry recording is available to export";
      return false;
    }

    const auto Exported = flying::core_sim::export_telemetry_csv(OutputPath, StoredRecording);
    if (!Exported.exported)
    {
      LastStatus = Exported.errors.empty() ? "Telemetry CSV export failed" : Exported.errors.front();
      return false;
    }
    LastStatus = "Telemetry CSV exported";
    return true;
  }

  bool ExportJson(const std::filesystem::path& OutputPath)
  {
    if (OutputPath.empty())
    {
      LastStatus = "Telemetry JSON export path is empty";
      return false;
    }
    if (Recorder)
    {
      StoredRecording = Recorder->recording();
      bHasStoredRecording = true;
    }
    if (!bHasStoredRecording)
    {
      LastStatus = "No telemetry recording is available to export";
      return false;
    }

    const auto Exported = flying::core_sim::export_telemetry_json(OutputPath, StoredRecording);
    if (!Exported.exported)
    {
      LastStatus = Exported.errors.empty()
                     ? "Telemetry JSON export failed"
                     : Exported.errors.front();
      return false;
    }
    LastStatus = "Telemetry JSON exported";
    return true;
  }

  FFlyingCoreSimStateSnapshot Snapshot() const
  {
    const AuthoritativeState& State = Simulator.state();

    FFlyingCoreSimStateSnapshot Snapshot;
    Snapshot.bValid = true;
    Snapshot.SimulationTimeSeconds = State.simulation_time_s;
    Snapshot.StepIndex = static_cast<int64>(State.step_index);
    Snapshot.StateHash = static_cast<int64>(flying::core_sim::hash_state(State));
    Snapshot.EcefPositionMeters = ToUnrealVector(State.ecef_position_m);
    Snapshot.EcefVelocityMetersPerSecond = ToUnrealVector(State.ecef_velocity_mps);
    Snapshot.BodyToEcef = ToUnrealQuat(State.body_to_ecef);
    Snapshot.BodyToEcefQuaternionXyzw = FVector4(
      Snapshot.BodyToEcef.X,
      Snapshot.BodyToEcef.Y,
      Snapshot.BodyToEcef.Z,
      Snapshot.BodyToEcef.W);
    Snapshot.Weather = ToUnreal(State.weather);
    Snapshot.RelativeAirVelocityBodyMetersPerSecond =
      ToUnrealVector(State.relative_air_velocity_body_mps);
    Snapshot.WeatherDynamicPressurePascal = State.weather_dynamic_pressure_pa;
    return Snapshot;
  }

  FFlyingAircraftInstrumentSnapshot InstrumentSnapshot() const
  {
    return ToUnreal(Systems.instruments());
  }

  void SetSystemSwitch(const std::string& SwitchId, bool bEnabled)
  {
    auto& Switches = Systems.switches();
    if (SwitchId == "battery_master")
    {
      Switches.battery_master_on = bEnabled;
    }
    else if (SwitchId == "alternator")
    {
      Switches.alternator_on = bEnabled;
    }
    else if (SwitchId == "avionics_master")
    {
      Switches.avionics_master_on = bEnabled;
    }
    else if (SwitchId == "pitot_heat")
    {
      Switches.pitot_heat_on = bEnabled;
    }
    else if (SwitchId == "electric_fuel_pump")
    {
      Switches.electric_fuel_pump_on = bEnabled;
    }
    else if (SwitchId == "standby_vacuum_pump")
    {
      Switches.standby_vacuum_pump_on = bEnabled;
    }
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
  }

  void SetFuelSelector(const std::string& SelectorId)
  {
    if (SelectorId == "left")
    {
      Systems.fuel().set_selector(flying::core_sim::FuelTankSelector::left);
    }
    else if (SelectorId == "right")
    {
      Systems.fuel().set_selector(flying::core_sim::FuelTankSelector::right);
    }
    else if (SelectorId == "off")
    {
      Systems.fuel().set_selector(flying::core_sim::FuelTankSelector::off);
    }
    else
    {
      Systems.fuel().set_selector(flying::core_sim::FuelTankSelector::both);
    }
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
  }

  void SetFuelWeight(double TotalFuelWeightKg)
  {
    RequestedFuelMassKg = std::clamp(TotalFuelWeightKg, 0.0, ActiveFuelCapacityKg);
    flying::core_sim::FuelTankState Tanks;
    const double ClampedFuelKg = RequestedFuelMassKg;
    Tanks.left_quantity_kg = ClampedFuelKg * 0.5;
    Tanks.right_quantity_kg = ClampedFuelKg - Tanks.left_quantity_kg;
    Tanks.left_capacity_kg = ActiveFuelCapacityKg * 0.5;
    Tanks.right_capacity_kg = ActiveFuelCapacityKg - Tanks.left_capacity_kg;
    Tanks.selector = flying::core_sim::FuelTankSelector::both;
    Systems.fuel().reset(Tanks);
    ApplyMassBalance(ClampedFuelKg, Simulator.aircraft_mass_balance().payload_mass_kg);
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
  }

  void SetLoadedWeight(double PilotAndPayloadWeightKg)
  {
    RequestedPayloadMassKg = std::clamp(PilotAndPayloadWeightKg, 0.0, ActiveMaxPayloadKg);
    ApplyMassBalance(
      Simulator.aircraft_mass_balance().fuel_mass_kg,
      RequestedPayloadMassKg);
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
  }

  void SetAircraftId(std::string AircraftId)
  {
    if (AircraftId.empty())
    {
      AircraftId = TCHAR_TO_UTF8(kDefaultAircraftId);
    }
    ActiveAircraftId = std::move(AircraftId);
    LastStatus = "Aircraft selected: " + ActiveAircraftId;
  }

  void SetFailure(const std::string& FailureId, bool bFailed)
  {
    auto& Failures = Systems.failures();
    if (FailureId == "battery")
    {
      Failures.battery_failed = bFailed;
    }
    else if (FailureId == "alternator")
    {
      Failures.alternator_failed = bFailed;
    }
    else if (FailureId == "avionics_bus")
    {
      Failures.avionics_bus_failed = bFailed;
    }
    else if (FailureId == "fuel_left_tank")
    {
      Failures.fuel_left_tank_blocked = bFailed;
    }
    else if (FailureId == "fuel_right_tank")
    {
      Failures.fuel_right_tank_blocked = bFailed;
    }
    else if (FailureId == "engine_driven_fuel_pump")
    {
      Failures.engine_driven_fuel_pump_failed = bFailed;
    }
    else if (FailureId == "electric_fuel_pump")
    {
      Failures.electric_fuel_pump_failed = bFailed;
    }
    else if (FailureId == "vacuum_pump")
    {
      Failures.vacuum_pump_failed = bFailed;
    }
    else if (FailureId == "standby_vacuum_pump")
    {
      Failures.standby_vacuum_pump_failed = bFailed;
    }
    else if (FailureId == "pitot")
    {
      Failures.pitot_blocked = bFailed;
    }
    else if (FailureId == "static")
    {
      Failures.static_port_blocked = bFailed;
    }
    else if (FailureId == "pitot_heat")
    {
      Failures.pitot_heat_failed = bFailed;
    }
    else if (FailureId == "gps")
    {
      Failures.gps_failed = bFailed;
    }
    else if (FailureId == "engine_sensor_power")
    {
      Failures.engine_sensor_power_failed = bFailed;
    }
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
  }

  void SetManualWeather(WeatherScenario Scenario)
  {
    Simulator.set_manual_weather_scenario(std::move(Scenario));
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
  }

  void ApplyMassBalance(double FuelMassKg, double PayloadMassKg)
  {
    flying::core_sim::AircraftMassBalanceState MassBalance = Simulator.aircraft_mass_balance();
    const double PreviousTotalMassKg = std::max(1.0, MassBalance.total_mass_kg);
    const double EmptyMassKg =
      std::max(1.0, PreviousTotalMassKg - MassBalance.fuel_mass_kg - MassBalance.payload_mass_kg);

    MassBalance.fuel_mass_kg = std::max(0.0, FuelMassKg);
    MassBalance.payload_mass_kg = std::max(0.0, PayloadMassKg);
    MassBalance.total_mass_kg = EmptyMassKg + MassBalance.fuel_mass_kg + MassBalance.payload_mass_kg;

    const double InertiaScale = MassBalance.total_mass_kg / PreviousTotalMassKg;
    MassBalance.inertia_tensor_kg_m2.ixx =
      std::max(1.0, MassBalance.inertia_tensor_kg_m2.ixx * InertiaScale);
    MassBalance.inertia_tensor_kg_m2.iyy =
      std::max(1.0, MassBalance.inertia_tensor_kg_m2.iyy * InertiaScale);
    MassBalance.inertia_tensor_kg_m2.izz =
      std::max(1.0, MassBalance.inertia_tensor_kg_m2.izz * InertiaScale);
    MassBalance.cg_within_envelope = true;
    Simulator.set_aircraft_mass_balance(MassBalance);
  }

  void StepSystems(double DeltaSeconds,
                   const AircraftControlInputSample& AircraftControls,
                   bool bEngineRunning)
  {
    AircraftSystemsInput Input;
    Input.truth = MakeSystemsTruth(Simulator.state());
    Input.controls = AircraftControls;
    Input.engine_rpm = EstimateEngineRpm(AircraftControls, bEngineRunning);
    Input.weather = Simulator.weather();
    Input.weather_valid = true;
    Input.outside_air_temperature_k = Input.weather.atmosphere.temperature_k;
    Systems.step(std::max(0.0, DeltaSeconds), Input);
  }

  bool IsRecording() const
  {
    return Recorder.has_value();
  }

  bool HasReplayLoaded() const
  {
    return bHasStoredRecording;
  }

  const flying::core_sim::TelemetryRecording* AvailableRecording() const
  {
    if (Recorder)
    {
      return &Recorder->recording();
    }
    return bHasStoredRecording ? &StoredRecording : nullptr;
  }
};

UFlyingCoreSimComponent::UFlyingCoreSimComponent()
{
  PrimaryComponentTick.bCanEverTick = true;
}

UFlyingCoreSimComponent::~UFlyingCoreSimComponent() = default;

void UFlyingCoreSimComponent::BeginPlay()
{
  Super::BeginPlay();

  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  InitialLatitudeDegrees = Settings->PilotOriginLatitudeDegrees;
  InitialLongitudeDegrees = Settings->PilotOriginLongitudeDegrees;
  InitialAltitudeMeters = Settings->PilotOriginHeightMeters;
  InitialVelocityEnuMetersPerSecond.X =
    Settings->DefaultAircraftInitialSpeedEastMetersPerSecond;

  if (bUseScenarioSelectionOnBeginPlay)
  {
    StartScenario(InitialScenario);
  }
  else
  {
    ResetCoreSim();
  }
}

void UFlyingCoreSimComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
  Bridge.Reset();
  PublishTelemetryStatus();
  Super::EndPlay(EndPlayReason);
}

void UFlyingCoreSimComponent::TickComponent(
  float DeltaTime,
  ELevelTick TickType,
  FActorComponentTickFunction* ThisTickFunction)
{
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  if (bAutoAdvance)
  {
    UpdatePerformanceCounters(static_cast<double>(DeltaTime));
    AdvanceCoreSim(static_cast<double>(DeltaTime));
  }
}

void UFlyingCoreSimComponent::ResetCoreSim()
{
  EnsureBridge();
  try
  {
    Bridge->Reset(
      InitialLatitudeDegrees,
      InitialLongitudeDegrees,
      InitialAltitudeMeters,
      InitialVelocityEnuMetersPerSecond);
    PublishSnapshot();
    PublishTelemetryStatus();
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim reset failed: %s"), ANSI_TO_TCHAR(Error.what()));
    CurrentSnapshot = {};
    CurrentInstrumentSnapshot = {};
    if (Bridge)
    {
      Bridge->LastStatus = Error.what();
    }
    PublishTelemetryStatus();
  }
}

bool UFlyingCoreSimComponent::StartScenario(const FFlyingScenarioSelection& Selection)
{
  EnsureBridge();
  try
  {
    const flying::core_sim::ScenarioInitialState ScenarioState =
      Bridge->ResetScenario(ToCore(Selection));
    CurrentScenarioState = ToUnreal(ScenarioState);
    PublishSnapshot();
    PublishTelemetryStatus();
    return true;
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim scenario start failed: %s"), ANSI_TO_TCHAR(Error.what()));
    CurrentSnapshot = {};
    CurrentInstrumentSnapshot = {};
    CurrentScenarioState = {};
    if (Bridge)
    {
      Bridge->LastStatus = Error.what();
    }
    PublishTelemetryStatus();
    return false;
  }
}

bool UFlyingCoreSimComponent::StartScenarioAtPosition(
  double LatitudeDegrees,
  double LongitudeDegrees,
  double AltitudeMeters,
  double TrueHeadingDegrees,
  EFlyingScenarioStartMode StartMode)
{
  EnsureBridge();
  try
  {
    const flying::core_sim::ScenarioInitialState ScenarioState =
      Bridge->ResetScenarioAtPosition(
        LatitudeDegrees,
        LongitudeDegrees,
        AltitudeMeters,
        TrueHeadingDegrees,
        ToCore(StartMode));
    CurrentScenarioState = ToUnreal(ScenarioState);
    PublishSnapshot();
    PublishTelemetryStatus();
    return true;
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim custom-position scenario start failed: %s"), ANSI_TO_TCHAR(Error.what()));
    CurrentSnapshot = {};
    CurrentInstrumentSnapshot = {};
    CurrentScenarioState = {};
    if (Bridge)
    {
      Bridge->LastStatus = Error.what();
    }
    PublishTelemetryStatus();
    return false;
  }
}

TArray<FFlyingScenarioLocation> UFlyingCoreSimComponent::GetPilotScenarioLocations() const
{
  TArray<FFlyingScenarioLocation> Result;
  const std::vector<flying::core_sim::PilotScenarioLocation> Locations =
    flying::core_sim::default_pilot_scenario_locations();
  Result.Reserve(static_cast<int32>(Locations.size()));
  for (const flying::core_sim::PilotScenarioLocation& Location : Locations)
  {
    Result.Add(ToUnreal(Location));
  }
  return Result;
}

const FFlyingScenarioRuntimeState& UFlyingCoreSimComponent::GetCurrentScenarioState() const
{
  return CurrentScenarioState;
}

void UFlyingCoreSimComponent::AdvanceCoreSim(double DeltaSeconds)
{
  EnsureBridge();
  const double ClampedDeltaSeconds =
    MaxAdvanceDeltaSeconds > 0.0 ? std::min(DeltaSeconds, MaxAdvanceDeltaSeconds) : DeltaSeconds;

  try
  {
    const AdvanceReport Report = Bridge->Advance(ClampedDeltaSeconds);
    // This counter validates executed 240 Hz steps for the interval CoreSim was
    // asked to advance; the max-advance clamp is a separate hitch protection.
    const int32 ExpectedSteps =
      FMath::Max(0, FMath::RoundToInt(ClampedDeltaSeconds / Report.fixed_step_s));
    MaxCoreSimStepsPerFrame =
      FMath::Max(MaxCoreSimStepsPerFrame, static_cast<int32>(Report.steps_executed));
    if (Report.steps_executed < static_cast<uint32>(ExpectedSteps))
    {
      CoreSimMissedStepCount +=
        static_cast<int64>(ExpectedSteps - static_cast<int32>(Report.steps_executed));
    }
    PublishSnapshot();
    PublishTelemetryStatus();
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim advance failed: %s"), ANSI_TO_TCHAR(Error.what()));
  }
}

void UFlyingCoreSimComponent::AdvanceCoreSimWithInputs(
  double DeltaSeconds,
  FVector ForceBodyNewtons,
  FVector MomentBodyNewtonMeters,
  const FFlyingMappedInputState& MappedInputState,
  bool bEngineRunning)
{
  LastMappedInputState = MappedInputState;
  LastInputSampleSeconds = FPlatformTime::Seconds();
  EnsureBridge();
  const double ClampedDeltaSeconds =
    MaxAdvanceDeltaSeconds > 0.0 ? std::min(DeltaSeconds, MaxAdvanceDeltaSeconds) : DeltaSeconds;

  try
  {
    const AdvanceReport Report = Bridge->Advance(
      ClampedDeltaSeconds,
      ToCoreInput(ForceBodyNewtons, MomentBodyNewtonMeters),
      ToCoreAircraftControls(MappedInputState),
      bEngineRunning);
    // This counter validates executed 240 Hz steps for the interval CoreSim was
    // asked to advance; the max-advance clamp is a separate hitch protection.
    const int32 ExpectedSteps =
      FMath::Max(0, FMath::RoundToInt(ClampedDeltaSeconds / Report.fixed_step_s));
    MaxCoreSimStepsPerFrame =
      FMath::Max(MaxCoreSimStepsPerFrame, static_cast<int32>(Report.steps_executed));
    if (Report.steps_executed < static_cast<uint32>(ExpectedSteps))
    {
      CoreSimMissedStepCount +=
        static_cast<int64>(ExpectedSteps - static_cast<int32>(Report.steps_executed));
    }
    PublishSnapshot();
    LastCoreSimInputProcessingMilliseconds =
      (FPlatformTime::Seconds() - LastInputSampleSeconds) * 1000.0;
    PublishTelemetryStatus();
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim input advance failed: %s"), ANSI_TO_TCHAR(Error.what()));
    if (Bridge)
    {
      Bridge->LastStatus = Error.what();
    }
    PublishTelemetryStatus();
  }
}

void UFlyingCoreSimComponent::ApplyMappedAircraftControls(
  const FFlyingMappedInputState& MappedInputState,
  bool bEngineRunning)
{
  LastMappedInputState = MappedInputState;
  LastInputSampleSeconds = FPlatformTime::Seconds();
  EnsureBridge();
  try
  {
    Bridge->ApplyAircraftControls(ToCoreAircraftControls(MappedInputState), bEngineRunning);
    PublishSnapshot();
    LastCoreSimInputProcessingMilliseconds =
      (FPlatformTime::Seconds() - LastInputSampleSeconds) * 1000.0;
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim cockpit control update failed: %s"), ANSI_TO_TCHAR(Error.what()));
    if (Bridge)
    {
      Bridge->LastStatus = Error.what();
    }
    PublishTelemetryStatus();
  }
}

const FFlyingCoreSimStateSnapshot& UFlyingCoreSimComponent::GetCurrentSnapshot() const
{
  return CurrentSnapshot;
}

const FFlyingAircraftInstrumentSnapshot& UFlyingCoreSimComponent::GetCurrentInstrumentSnapshot() const
{
  return CurrentInstrumentSnapshot;
}

void UFlyingCoreSimComponent::SetManualWeatherScenario(
  const FFlyingManualWeatherScenario& WeatherScenario)
{
  EnsureBridge();
  try
  {
    Bridge->SetManualWeather(ToCore(WeatherScenario));
    PublishSnapshot();
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim weather update failed: %s"), ANSI_TO_TCHAR(Error.what()));
    if (Bridge)
    {
      Bridge->LastStatus = Error.what();
    }
    PublishTelemetryStatus();
  }
}

void UFlyingCoreSimComponent::SetAircraftSystemSwitch(FName SwitchId, bool bEnabled)
{
  EnsureBridge();
  Bridge->SetSystemSwitch(ToStdString(SwitchId), bEnabled);
  PublishSnapshot();
}

void UFlyingCoreSimComponent::SetAircraftFuelSelector(FName SelectorId)
{
  EnsureBridge();
  Bridge->SetFuelSelector(ToStdString(SelectorId));
  PublishSnapshot();
}

void UFlyingCoreSimComponent::SetAircraftFailure(FName FailureId, bool bFailed)
{
  EnsureBridge();
  Bridge->SetFailure(ToStdString(FailureId), bFailed);
  PublishSnapshot();
}

void UFlyingCoreSimComponent::SetAircraftId(const FString& AircraftId)
{
  EnsureBridge();
  CurrentAircraftId = AircraftId.IsEmpty() ? kDefaultAircraftId : AircraftId;
  Bridge->SetAircraftId(ToStdString(CurrentAircraftId));
  PublishTelemetryStatus();
}

FString UFlyingCoreSimComponent::GetAircraftId() const
{
  return CurrentAircraftId;
}

void UFlyingCoreSimComponent::SetAircraftFuelWeight(double TotalFuelWeightKg)
{
  EnsureBridge();
  Bridge->SetFuelWeight(TotalFuelWeightKg);
  PublishSnapshot();
}

void UFlyingCoreSimComponent::SetAircraftLoadedWeight(double PilotAndPayloadWeightKg)
{
  AircraftLoadedWeightKg = FMath::Max(0.0, PilotAndPayloadWeightKg);
  EnsureBridge();
  Bridge->SetLoadedWeight(AircraftLoadedWeightKg);
  PublishSnapshot();
}

double UFlyingCoreSimComponent::GetAircraftLoadedWeightKg() const
{
  return AircraftLoadedWeightKg;
}

const FFlyingMappedInputState& UFlyingCoreSimComponent::GetLastMappedInputState() const
{
  return LastMappedInputState;
}

int64 UFlyingCoreSimComponent::GetCoreSimMissedStepCount() const
{
  return CoreSimMissedStepCount;
}

int32 UFlyingCoreSimComponent::GetMaxCoreSimStepsPerFrame() const
{
  return MaxCoreSimStepsPerFrame;
}

double UFlyingCoreSimComponent::GetAverageFrameRate() const
{
  return AverageFrameRate;
}

double UFlyingCoreSimComponent::GetOnePercentLowFrameRate() const
{
  return OnePercentLowFrameRate;
}

double UFlyingCoreSimComponent::GetMaxObservedHitchMilliseconds() const
{
  return MaxObservedHitchMilliseconds;
}

double UFlyingCoreSimComponent::GetLastCoreSimInputProcessingMilliseconds() const
{
  return LastCoreSimInputProcessingMilliseconds;
}

bool UFlyingCoreSimComponent::StartTelemetryRecording(
  const FString& OutputPath,
  const FString& SessionId)
{
  EnsureBridge();
  try
  {
    const bool bStarted =
      Bridge->StartRecording(ToPath(OutputPath), ToStdString(SessionId), CurrentScenarioState);
    PublishTelemetryStatus();
    return bStarted;
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("Telemetry recording start failed: %s"), ANSI_TO_TCHAR(Error.what()));
    if (Bridge)
    {
      Bridge->LastStatus = Error.what();
    }
    PublishTelemetryStatus();
    return false;
  }
}

bool UFlyingCoreSimComponent::StopTelemetryRecording()
{
  EnsureBridge();
  const bool bStopped = Bridge->StopRecording(true);
  PublishTelemetryStatus();
  return bStopped;
}

bool UFlyingCoreSimComponent::SaveTelemetryRecording(const FString& OutputPath)
{
  EnsureBridge();
  const bool bSaved = Bridge->SaveRecording(ToPath(OutputPath));
  PublishTelemetryStatus();
  return bSaved;
}

bool UFlyingCoreSimComponent::LoadTelemetryReplay(
  const FString& InputPath,
  bool bWarnOnIncompatible)
{
  EnsureBridge();
  const auto Policy =
    bWarnOnIncompatible
      ? flying::core_sim::ReplayCompatibilityPolicy::WarnOnMismatch
      : flying::core_sim::ReplayCompatibilityPolicy::RefuseOnMismatch;
  const bool bLoaded = Bridge->LoadReplay(ToPath(InputPath), Policy);
  PublishTelemetryStatus();
  return bLoaded;
}

bool UFlyingCoreSimComponent::PlayLoadedTelemetryReplay(bool bWarnOnIncompatible)
{
  EnsureBridge();
  const auto Policy =
    bWarnOnIncompatible
      ? flying::core_sim::ReplayCompatibilityPolicy::WarnOnMismatch
      : flying::core_sim::ReplayCompatibilityPolicy::RefuseOnMismatch;
  const bool bPlayed = Bridge->PlayReplay(Policy);
  PublishSnapshot();
  PublishTelemetryStatus();
  return bPlayed;
}

bool UFlyingCoreSimComponent::ScrubTelemetryReplayNormalized(double PositionNorm)
{
  EnsureBridge();
  const bool bScrubbed = Bridge->ScrubReplay(PositionNorm);
  PublishSnapshot();
  PublishTelemetryStatus();
  return bScrubbed;
}

bool UFlyingCoreSimComponent::ExportTelemetryCsv(const FString& OutputPath)
{
  EnsureBridge();
  const bool bExported = Bridge->ExportCsv(ToPath(OutputPath));
  PublishTelemetryStatus();
  return bExported;
}

bool UFlyingCoreSimComponent::ExportTelemetryJson(const FString& OutputPath)
{
  EnsureBridge();
  const bool bExported = Bridge->ExportJson(ToPath(OutputPath));
  PublishTelemetryStatus();
  return bExported;
}

TArray<FFlyingTelemetryRoutePoint> UFlyingCoreSimComponent::GetTelemetryRoutePoints() const
{
  TArray<FFlyingTelemetryRoutePoint> Result;
  if (!Bridge)
  {
    return Result;
  }

  const flying::core_sim::TelemetryRecording* Recording = Bridge->AvailableRecording();
  if (!Recording)
  {
    return Result;
  }

  Result.Reserve(static_cast<int32>(Recording->frames.size()));
  for (const flying::core_sim::TelemetryFrame& Frame : Recording->frames)
  {
    const EcefPosition Ecef{
      {Frame.state.ecef_position_m.x, Frame.state.ecef_position_m.y, Frame.state.ecef_position_m.z}};
    const GeodeticCoordinates Geodetic = flying::geo_terrain::ecef_to_geodetic(Ecef);
    FFlyingTelemetryRoutePoint Point;
    Point.LatitudeDegrees = Geodetic.latitude_degrees();
    Point.LongitudeDegrees = Geodetic.longitude_degrees();
    Point.AltitudeMeters = Geodetic.ellipsoidal_height.meters;
    Point.TimeSeconds = Frame.state.simulation_time_s;
    Result.Add(Point);
  }
  return Result;
}

TArray<FFlyingTelemetryGraphSeries> UFlyingCoreSimComponent::GetTelemetryGraphSeries() const
{
  TArray<FFlyingTelemetryGraphSeries> Result;
  if (!Bridge)
  {
    return Result;
  }

  const flying::core_sim::TelemetryRecording* Recording = Bridge->AvailableRecording();
  if (!Recording)
  {
    return Result;
  }

  FFlyingTelemetryGraphSeries Altitude;
  Altitude.SeriesId = TEXT("altitude_m");
  Altitude.DisplayName = TEXT("Altitude (m)");
  FFlyingTelemetryGraphSeries Airspeed;
  Airspeed.SeriesId = TEXT("airspeed_mps");
  Airspeed.DisplayName = TEXT("Airspeed (m/s)");
  FFlyingTelemetryGraphSeries Throttle;
  Throttle.SeriesId = TEXT("throttle_norm");
  Throttle.DisplayName = TEXT("Throttle");

  Altitude.Points.Reserve(static_cast<int32>(Recording->frames.size()));
  Airspeed.Points.Reserve(static_cast<int32>(Recording->frames.size()));
  Throttle.Points.Reserve(static_cast<int32>(Recording->frames.size()));
  for (const flying::core_sim::TelemetryFrame& Frame : Recording->frames)
  {
    const EcefPosition Ecef{
      {Frame.state.ecef_position_m.x, Frame.state.ecef_position_m.y, Frame.state.ecef_position_m.z}};
    const GeodeticCoordinates Geodetic = flying::geo_terrain::ecef_to_geodetic(Ecef);

    FFlyingTelemetryGraphPoint AltitudePoint;
    AltitudePoint.TimeSeconds = Frame.state.simulation_time_s;
    AltitudePoint.Value = Geodetic.ellipsoidal_height.meters;
    Altitude.Points.Add(AltitudePoint);

    FFlyingTelemetryGraphPoint AirspeedPoint;
    AirspeedPoint.TimeSeconds = Frame.state.simulation_time_s;
    AirspeedPoint.Value = std::sqrt(
      Frame.state.relative_air_velocity_body_mps.x * Frame.state.relative_air_velocity_body_mps.x +
      Frame.state.relative_air_velocity_body_mps.y * Frame.state.relative_air_velocity_body_mps.y +
      Frame.state.relative_air_velocity_body_mps.z * Frame.state.relative_air_velocity_body_mps.z);
    Airspeed.Points.Add(AirspeedPoint);

    FFlyingTelemetryGraphPoint ThrottlePoint;
    ThrottlePoint.TimeSeconds = Frame.state.simulation_time_s;
    ThrottlePoint.Value = Frame.aircraft_controls.throttle_norm;
    Throttle.Points.Add(ThrottlePoint);
  }

  Result.Add(Altitude);
  Result.Add(Airspeed);
  Result.Add(Throttle);
  return Result;
}

void UFlyingCoreSimComponent::EnsureBridge()
{
  if (!Bridge)
  {
    Bridge = MakeUnique<FFlyingCoreSimBridgeImpl>();
  }
}

void UFlyingCoreSimComponent::PublishSnapshot()
{
  CurrentSnapshot = Bridge ? Bridge->Snapshot() : FFlyingCoreSimStateSnapshot{};
  CurrentInstrumentSnapshot = Bridge ? Bridge->InstrumentSnapshot() : FFlyingAircraftInstrumentSnapshot{};
}

void UFlyingCoreSimComponent::UpdatePerformanceCounters(double DeltaSeconds)
{
  if (DeltaSeconds <= 0.0)
  {
    return;
  }

  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  const double HitchMilliseconds = DeltaSeconds * 1000.0;
  MaxObservedHitchMilliseconds =
    FMath::Max(MaxObservedHitchMilliseconds, HitchMilliseconds);

  constexpr int32 kFrameWindowCapacity = 3600;
  if (FrameTimeWindowSeconds.Num() < kFrameWindowCapacity)
  {
    FrameTimeWindowSeconds.Add(DeltaSeconds);
    FrameTimeWindowTotalSeconds += DeltaSeconds;
  }
  else
  {
    FrameTimeWindowTotalSeconds -= FrameTimeWindowSeconds[FrameTimeWindowWriteIndex];
    FrameTimeWindowSeconds[FrameTimeWindowWriteIndex] = DeltaSeconds;
    FrameTimeWindowTotalSeconds += DeltaSeconds;
    FrameTimeWindowWriteIndex =
      (FrameTimeWindowWriteIndex + 1) % kFrameWindowCapacity;
  }

  if (FrameTimeWindowTotalSeconds > 0.0)
  {
    AverageFrameRate =
      static_cast<double>(FrameTimeWindowSeconds.Num()) / FrameTimeWindowTotalSeconds;
  }

  ++PerformanceCounterFrameIndex;
  if (PerformanceCounterFrameIndex % 15 != 0 &&
      OnePercentLowFrameRate > 0.0 &&
      HitchMilliseconds <= Settings->MaximumStreamingHitchMilliseconds)
  {
    return;
  }

  TArray<double> SortedFrameTimes = FrameTimeWindowSeconds;
  SortedFrameTimes.Sort();
  const int32 SlowFrameCount = FMath::Max(1, FMath::CeilToInt(
    static_cast<double>(SortedFrameTimes.Num()) * 0.01));
  double SlowFrameTotalSeconds = 0.0;
  for (int32 Index = 0; Index < SlowFrameCount; ++Index)
  {
    SlowFrameTotalSeconds += SortedFrameTimes[SortedFrameTimes.Num() - 1 - Index];
  }
  if (SlowFrameTotalSeconds > 0.0)
  {
    OnePercentLowFrameRate =
      static_cast<double>(SlowFrameCount) / SlowFrameTotalSeconds;
  }
}

void UFlyingCoreSimComponent::PublishTelemetryStatus()
{
  bTelemetryRecording = Bridge ? Bridge->IsRecording() : false;
  bReplayLoaded = Bridge ? Bridge->HasReplayLoaded() : false;
  LastTelemetryStatus = Bridge ? ToFString(Bridge->LastStatus) : FString();
}
