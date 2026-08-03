#include "FlyingCoreSimComponent.h"

#include "FlyingPresentationSettings.h"
#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/simulator.hpp"
#include "flying/geo_terrain/geodesy.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

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

  ResetCoreSim();
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
