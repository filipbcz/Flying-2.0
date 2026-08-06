#include "FlyingCoreSimComponent.h"

#include "FlyingPresentationSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/aircraft_systems.hpp"
#include "flying/core_sim/scenario.hpp"
#include "flying/core_sim/simulator.hpp"
#include "flying/core_sim/telemetry.hpp"
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
using flying::core_sim::Quaterniond;
using flying::core_sim::ReplayEnvironment;
using flying::core_sim::Vector3d;
using flying::geo_terrain::EcefPosition;
using flying::geo_terrain::EnuVector;
using flying::geo_terrain::GeodeticCoordinates;
using flying::geo_terrain::LocalTangentFrame;

const TCHAR* const kTerrainPackageSchemaVersion = TEXT("flying.terrain-package.v1");
const TCHAR* const kPilotRegionPackageSchemaVersion = TEXT("flying.pilot-region-package.v1");

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
  bool bLastEngineRunning = false;
  std::optional<flying::core_sim::TelemetryRecorder> Recorder;
  flying::core_sim::TelemetryRecording StoredRecording;
  bool bHasStoredRecording = false;
  std::filesystem::path ActiveTelemetryPath;
  std::string LastStatus;

  void Reset(double LatitudeDegrees,
             double LongitudeDegrees,
             double HeightMeters,
             const FVector& InitialVelocityEnuMetersPerSecond)
  {
    Recorder.reset();
    Simulator.reset(MakeInitialState(
      LatitudeDegrees,
      LongitudeDegrees,
      HeightMeters,
      InitialVelocityEnuMetersPerSecond));
    Systems.reset();
    LastAircraftControls = Simulator.initial_aircraft_controls();
    bLastEngineRunning = LastAircraftControls.mixture_norm > 0.0;
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
    LastStatus = "CoreSim reset; active telemetry recording stopped";
  }

  void Advance(double DeltaSeconds)
  {
    Advance(
      DeltaSeconds,
      ControlInputSample{},
      Simulator.initial_aircraft_controls(),
      Simulator.initial_aircraft_controls().mixture_norm > 0.0);
  }

  void Advance(double DeltaSeconds,
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
    flying::core_sim::ScenarioInitialState State =
      flying::core_sim::reset_simulator_to_scenario(Simulator, Selection);
    Systems.reset();
    LastAircraftControls = Simulator.initial_aircraft_controls();
    bLastEngineRunning = State.engine_running;
    StepSystems(0.0, LastAircraftControls, bLastEngineRunning);
    LastStatus = "Scenario started; active telemetry recording stopped";
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
    else if (FailureId == "vacuum_pump")
    {
      Failures.vacuum_pump_failed = bFailed;
    }
    else if (FailureId == "pitot")
    {
      Failures.pitot_blocked = bFailed;
    }
    else if (FailureId == "static")
    {
      Failures.static_port_blocked = bFailed;
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

  void StepSystems(double DeltaSeconds,
                   const AircraftControlInputSample& AircraftControls,
                   bool bEngineRunning)
  {
    AircraftSystemsInput Input;
    Input.truth = MakeSystemsTruth(Simulator.state());
    Input.controls = AircraftControls;
    Input.engine_rpm = EstimateEngineRpm(AircraftControls, bEngineRunning);
    Input.outside_air_temperature_k = 288.15;
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
    Bridge->Advance(ClampedDeltaSeconds);
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
  EnsureBridge();
  const double ClampedDeltaSeconds =
    MaxAdvanceDeltaSeconds > 0.0 ? std::min(DeltaSeconds, MaxAdvanceDeltaSeconds) : DeltaSeconds;

  try
  {
    Bridge->Advance(
      ClampedDeltaSeconds,
      ToCoreInput(ForceBodyNewtons, MomentBodyNewtonMeters),
      ToCoreAircraftControls(MappedInputState),
      bEngineRunning);
    PublishSnapshot();
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
  EnsureBridge();
  try
  {
    Bridge->ApplyAircraftControls(ToCoreAircraftControls(MappedInputState), bEngineRunning);
    PublishSnapshot();
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

void UFlyingCoreSimComponent::PublishTelemetryStatus()
{
  bTelemetryRecording = Bridge ? Bridge->IsRecording() : false;
  bReplayLoaded = Bridge ? Bridge->HasReplayLoaded() : false;
  LastTelemetryStatus = Bridge ? ToFString(Bridge->LastStatus) : FString();
}
