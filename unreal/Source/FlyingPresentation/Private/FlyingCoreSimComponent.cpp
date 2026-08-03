#include "FlyingCoreSimComponent.h"

#include "FlyingPresentationSettings.h"
#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/scenario.hpp"
#include "flying/core_sim/simulator.hpp"
#include "flying/geo_terrain/geodesy.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using flying::core_sim::AuthoritativeState;
using flying::core_sim::ControlInputSample;
using flying::core_sim::CoreSimulator;
using flying::core_sim::Quaterniond;
using flying::core_sim::Vector3d;
using flying::geo_terrain::EcefPosition;
using flying::geo_terrain::EnuVector;
using flying::geo_terrain::GeodeticCoordinates;
using flying::geo_terrain::LocalTangentFrame;

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

  void Reset(double LatitudeDegrees,
             double LongitudeDegrees,
             double HeightMeters,
             const FVector& InitialVelocityEnuMetersPerSecond)
  {
    Simulator.reset(MakeInitialState(
      LatitudeDegrees,
      LongitudeDegrees,
      HeightMeters,
      InitialVelocityEnuMetersPerSecond));
  }

  void Advance(double DeltaSeconds)
  {
    Simulator.advance(DeltaSeconds, ControlInputSample{});
  }

  flying::core_sim::ScenarioInitialState ResetScenario(
    const flying::core_sim::ScenarioSelection& Selection)
  {
    return flying::core_sim::reset_simulator_to_scenario(Simulator, Selection);
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
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim reset failed: %s"), ANSI_TO_TCHAR(Error.what()));
    CurrentSnapshot = {};
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
    return true;
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim scenario start failed: %s"), ANSI_TO_TCHAR(Error.what()));
    CurrentSnapshot = {};
    CurrentScenarioState = {};
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
  }
  catch (const std::exception& Error)
  {
    UE_LOG(LogTemp, Error, TEXT("CoreSim advance failed: %s"), ANSI_TO_TCHAR(Error.what()));
  }
}

const FFlyingCoreSimStateSnapshot& UFlyingCoreSimComponent::GetCurrentSnapshot() const
{
  return CurrentSnapshot;
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
}
