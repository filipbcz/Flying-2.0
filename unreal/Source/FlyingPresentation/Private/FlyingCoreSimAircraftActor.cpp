#include "FlyingCoreSimAircraftActor.h"

#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "FlyingCesiumGeoreferenceComponent.h"
#include "FlyingCoreSimComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundWaveProcedural.h"
#include "UObject/ConstructorHelpers.h"

namespace {

const TCHAR* const kCubeMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
const TCHAR* const kCylinderMeshPath = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
const TCHAR* const kConeMeshPath = TEXT("/Engine/BasicShapes/Cone.Cone");
const TCHAR* const kBasicMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial");

FString BoolLabel(bool bValue)
{
  return bValue ? TEXT("ON") : TEXT("OFF");
}

float ControlPosition(const TArray<FFlyingCockpitControlState>& Controls,
                      EFlyingCockpitControl Control,
                      float DefaultValue)
{
  const FFlyingCockpitControlState* State = Controls.FindByPredicate(
    [Control](const FFlyingCockpitControlState& Candidate)
    {
      return Candidate.Control == Control;
    });
  return State ? static_cast<float>(State->PositionNorm) : DefaultValue;
}

bool ControlEngaged(const TArray<FFlyingCockpitControlState>& Controls,
                    EFlyingCockpitControl Control,
                    bool bDefaultValue)
{
  const FFlyingCockpitControlState* State = Controls.FindByPredicate(
    [Control](const FFlyingCockpitControlState& Candidate)
    {
      return Candidate.Control == Control;
    });
  return State ? State->bEngaged : bDefaultValue;
}

bool IsAttachedTo(const USceneComponent* Component, const USceneComponent* ExpectedRoot)
{
  for (const USceneComponent* Current = Component; Current; Current = Current->GetAttachParent())
  {
    if (Current == ExpectedRoot)
    {
      return true;
    }
  }
  return false;
}

bool SnapshotsMatch(const FFlyingCoreSimImmutableStateSnapshot& Left,
                    const FFlyingCoreSimImmutableStateSnapshot& Right)
{
  return Left.bValid == Right.bValid &&
         Left.SimulationTimeSeconds == Right.SimulationTimeSeconds &&
         Left.StepIndex == Right.StepIndex &&
         Left.StateHash == Right.StateHash &&
         Left.EcefPositionMeters.Equals(Right.EcefPositionMeters, KINDA_SMALL_NUMBER) &&
         Left.EcefVelocityMetersPerSecond.Equals(
           Right.EcefVelocityMetersPerSecond,
           KINDA_SMALL_NUMBER) &&
         Left.BodyToEcef.Equals(Right.BodyToEcef, KINDA_SMALL_NUMBER);
}

constexpr int32 kProceduralAudioSampleRate = 22050;
constexpr float kMinimumProceduralAudioQueueSeconds = 0.005f;
constexpr float kMaximumProceduralAudioQueueSeconds = 0.5f;

void QueueProceduralTone(USoundWaveProcedural* Sound,
                         float FrequencyHz,
                         float Amplitude,
                         float DurationSeconds,
                         double& PhaseRadians)
{
  if (!Sound)
  {
    return;
  }

  const int32 SampleCount = FMath::Max(
    1,
    FMath::CeilToInt(DurationSeconds * static_cast<float>(kProceduralAudioSampleRate)));
  constexpr double TwoPi = 2.0 * PI;
  const double PhaseStep =
    TwoPi * static_cast<double>(FrequencyHz) / static_cast<double>(kProceduralAudioSampleRate);

  TArray<int16> Samples;
  Samples.SetNumUninitialized(SampleCount);
  for (int32 Index = 0; Index < SampleCount; ++Index)
  {
    Samples[Index] = static_cast<int16>(
      FMath::Clamp(FMath::Sin(static_cast<float>(PhaseRadians)) * Amplitude, -1.0f, 1.0f) *
      32767.0f);
    PhaseRadians += PhaseStep;
    if (PhaseRadians >= TwoPi)
    {
      PhaseRadians -= TwoPi;
    }
  }
  Sound->QueueAudio(
    reinterpret_cast<const uint8*>(Samples.GetData()),
    Samples.Num() * sizeof(int16));
}

void ConfigureProceduralTone(USoundWaveProcedural* Sound,
                             float FrequencyHz,
                             float Amplitude,
                             double& PhaseRadians)
{
  if (!Sound)
  {
    return;
  }

  Sound->SetSampleRate(kProceduralAudioSampleRate);
  Sound->NumChannels = 1;
  Sound->Duration = 1.0f;
  Sound->bLooping = true;
  Sound->SoundGroup = SOUNDGROUP_Default;
  QueueProceduralTone(Sound, FrequencyHz, Amplitude, 1.0f, PhaseRadians);
}

} // namespace

AFlyingCoreSimAircraftActor::AFlyingCoreSimAircraftActor()
{
  PrimaryActorTick.bCanEverTick = true;

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
  RootComponent = SceneRoot;

  AircraftMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AircraftMesh"));
  AircraftMesh->SetupAttachment(SceneRoot);
  AircraftMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  AircraftMesh->SetRelativeScale3D(FVector(2.4, 0.75, 0.55));

  static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(
    kConeMeshPath);
  if (ConeMesh.Succeeded())
  {
    AircraftMesh->SetStaticMesh(ConeMesh.Object);
    AircraftMesh->SetRelativeRotation(FRotator(0.0, 90.0, 0.0));
  }

  CockpitRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CockpitRoot"));
  CockpitRoot->SetupAttachment(SceneRoot);
  CockpitRoot->SetRelativeLocation(FVector(48.0, 0.0, 22.0));

  InstrumentPanelMesh = AddPrimitiveMesh(
    TEXT("InstrumentPanel"),
    kCubeMeshPath,
    CockpitRoot,
    FVector(26.0, 0.0, -4.0),
    FVector(0.10, 1.35, 0.55),
    FRotator(0.0, 0.0, 0.0),
    FLinearColor(0.025, 0.030, 0.035));

  LeftWingMesh = AddPrimitiveMesh(
    TEXT("LeftWing"),
    kCubeMeshPath,
    SceneRoot,
    FVector(0.0, -95.0, 0.0),
    FVector(1.25, 3.9, 0.08),
    FRotator(0.0, 0.0, -4.0),
    FLinearColor(0.82, 0.86, 0.90));
  RightWingMesh = AddPrimitiveMesh(
    TEXT("RightWing"),
    kCubeMeshPath,
    SceneRoot,
    FVector(0.0, 95.0, 0.0),
    FVector(1.25, 3.9, 0.08),
    FRotator(0.0, 0.0, 4.0),
    FLinearColor(0.82, 0.86, 0.90));
  TailMesh = AddPrimitiveMesh(
    TEXT("Tail"),
    kCubeMeshPath,
    SceneRoot,
    FVector(-95.0, 0.0, 25.0),
    FVector(0.42, 1.1, 0.10),
    FRotator(0.0, 0.0, 0.0),
    FLinearColor(0.78, 0.82, 0.86));
  PropellerMesh = AddPrimitiveMesh(
    TEXT("Propeller"),
    kCubeMeshPath,
    SceneRoot,
    FVector(128.0, 0.0, 0.0),
    FVector(0.04, 1.75, 0.04),
    FRotator(0.0, 0.0, 0.0),
    FLinearColor(0.10, 0.10, 0.10));

  CockpitFloodLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CockpitFloodLight"));
  CockpitFloodLight->SetupAttachment(CockpitRoot);
  CockpitFloodLight->SetRelativeLocation(FVector(5.0, 0.0, 18.0));
  CockpitFloodLight->SetLightColor(FLinearColor(0.95, 0.35, 0.20));
  CockpitFloodLight->SetIntensity(0.0f);
  CockpitFloodLight->SetAttenuationRadius(180.0f);

  InstrumentBacklight = CreateDefaultSubobject<UPointLightComponent>(TEXT("InstrumentBacklight"));
  InstrumentBacklight->SetupAttachment(InstrumentPanelMesh);
  InstrumentBacklight->SetRelativeLocation(FVector(-12.0, 0.0, 0.0));
  InstrumentBacklight->SetLightColor(FLinearColor(0.25, 0.55, 1.0));
  InstrumentBacklight->SetIntensity(0.0f);
  InstrumentBacklight->SetAttenuationRadius(120.0f);

  CockpitCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CockpitCamera"));
  CockpitCamera->SetupAttachment(CockpitRoot);
  CockpitCamera->SetRelativeLocation(FVector(-22.0, 0.0, 12.0));
  CockpitCamera->SetRelativeRotation(FRotator(-6.0, 0.0, 0.0));
  CockpitCamera->SetFieldOfView(72.0f);

  InstrumentCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("InstrumentCamera"));
  InstrumentCamera->SetupAttachment(CockpitRoot);
  InstrumentCamera->SetRelativeLocation(FVector(-2.0, 0.0, 5.0));
  InstrumentCamera->SetRelativeRotation(FRotator(-9.0, 0.0, 0.0));
  InstrumentCamera->SetFieldOfView(54.0f);

  ExteriorSpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ExteriorSpringArm"));
  ExteriorSpringArm->SetupAttachment(SceneRoot);
  ExteriorSpringArm->TargetArmLength = 620.0f;
  ExteriorSpringArm->SetRelativeRotation(FRotator(-16.0, -32.0, 0.0));

  ExteriorCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ExteriorCamera"));
  ExteriorCamera->SetupAttachment(ExteriorSpringArm);
  ExteriorCamera->SetFieldOfView(60.0f);

  ReplayInspectionCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ReplayInspectionCamera"));
  ReplayInspectionCamera->SetupAttachment(SceneRoot);
  ReplayInspectionCamera->SetRelativeLocation(FVector(150.0, -230.0, 95.0));
  ReplayInspectionCamera->SetRelativeRotation(FRotator(-18.0, 118.0, 0.0));
  ReplayInspectionCamera->SetFieldOfView(46.0f);

  EngineSound = CreateDefaultSubobject<USoundWaveProcedural>(TEXT("EngineTone"));
  ConfigureProceduralTone(EngineSound, 95.0f, 0.42f, EngineTonePhaseRadians);
  PropellerSound = CreateDefaultSubobject<USoundWaveProcedural>(TEXT("PropellerTone"));
  ConfigureProceduralTone(PropellerSound, 145.0f, 0.28f, PropellerTonePhaseRadians);
  CabinSound = CreateDefaultSubobject<USoundWaveProcedural>(TEXT("CabinTone"));
  ConfigureProceduralTone(CabinSound, 55.0f, 0.18f, CabinTonePhaseRadians);
  AirflowSound = CreateDefaultSubobject<USoundWaveProcedural>(TEXT("AirflowTone"));
  ConfigureProceduralTone(AirflowSound, 320.0f, 0.12f, AirflowTonePhaseRadians);
  DamageSound = CreateDefaultSubobject<USoundWaveProcedural>(TEXT("DamageTone"));
  ConfigureProceduralTone(DamageSound, 38.0f, 0.55f, DamageTonePhaseRadians);

  EngineAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("EngineAudio"));
  EngineAudio->SetupAttachment(AircraftMesh);
  EngineAudio->bAutoActivate = true;
  EngineAudio->SetSound(EngineSound);
  PropellerAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("PropellerAudio"));
  PropellerAudio->SetupAttachment(PropellerMesh);
  PropellerAudio->bAutoActivate = true;
  PropellerAudio->SetSound(PropellerSound);
  CabinAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("CabinAudio"));
  CabinAudio->SetupAttachment(CockpitRoot);
  CabinAudio->bAutoActivate = true;
  CabinAudio->SetSound(CabinSound);
  AirflowAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("AirflowAudio"));
  AirflowAudio->SetupAttachment(SceneRoot);
  AirflowAudio->bAutoActivate = true;
  AirflowAudio->SetSound(AirflowSound);
  DamageAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("DamageAudio"));
  DamageAudio->SetupAttachment(SceneRoot);
  DamageAudio->bAutoActivate = true;
  DamageAudio->SetSound(DamageSound);

  BuildCockpitControls();
  BuildInstrumentPanel();
  SetCockpitCameraMode(ActiveCameraMode);

  CoreSimComponent = CreateDefaultSubobject<UFlyingCoreSimComponent>(TEXT("CoreSim"));
  GeoreferenceComponent =
    CreateDefaultSubobject<UFlyingCesiumGeoreferenceComponent>(TEXT("CesiumGeoreference"));
}

void AFlyingCoreSimAircraftActor::BeginPlay()
{
  Super::BeginPlay();

  GeoreferenceComponent->ConfigureOriginFromSettings();
  UpdatePresentationFromCoreSim();
}

void AFlyingCoreSimAircraftActor::Tick(float DeltaSeconds)
{
  Super::Tick(DeltaSeconds);
  UpdatePresentationFromCoreSim();
  QueueProceduralAudio(DeltaSeconds);
  if (PropellerMesh && CoreSimComponent)
  {
    const double Rpm = CoreSimComponent->GetCurrentInstrumentSnapshot().Engine.Rpm;
    PropellerMesh->AddLocalRotation(FRotator(0.0, 0.0, Rpm * DeltaSeconds * 0.75));
  }
}

void AFlyingCoreSimAircraftActor::ApplyWorldOffset(const FVector& InOffset, bool bWorldShift)
{
  Super::ApplyWorldOffset(InOffset, bWorldShift);

  if (bWorldShift)
  {
    UpdatePresentationFromCoreSim();
  }
}

void AFlyingCoreSimAircraftActor::UpdatePresentationFromCoreSim()
{
  if (!CoreSimComponent)
  {
    return;
  }

  UpdatePresentationFromImmutableSnapshot(CoreSimComponent->GetCurrentImmutableSnapshot());
  UpdateCockpitFromInstruments(CoreSimComponent->GetCurrentInstrumentSnapshot());
}

void AFlyingCoreSimAircraftActor::UpdatePresentationFromSnapshot(
  const FFlyingCoreSimStateSnapshot& Snapshot)
{
  if (!Snapshot.bValid || !GeoreferenceComponent)
  {
    return;
  }

  const FVector UnrealLocation =
    GeoreferenceComponent->TransformEcefPositionToUnreal(Snapshot.EcefPositionMeters);
  const FRotator UnrealRotation =
    GeoreferenceComponent->TransformBodyToUnrealRotator(Snapshot);

  SetActorLocationAndRotation(
    UnrealLocation,
    UnrealRotation,
    false,
    nullptr,
    ETeleportType::TeleportPhysics);
}

void AFlyingCoreSimAircraftActor::UpdatePresentationFromImmutableSnapshot(
  const FFlyingCoreSimImmutableStateSnapshot& Snapshot)
{
  if (!Snapshot.bValid || !GeoreferenceComponent)
  {
    return;
  }

  const FVector UnrealLocation =
    GeoreferenceComponent->TransformEcefPositionToUnreal(Snapshot.EcefPositionMeters);
  const FRotator UnrealRotation =
    GeoreferenceComponent->TransformBodyToUnrealRotator(Snapshot);

  SetActorLocationAndRotation(
    UnrealLocation,
    UnrealRotation,
    false,
    nullptr,
    ETeleportType::TeleportPhysics);
  LastAppliedImmutableSnapshot = Snapshot;
  LastAppliedUnrealLocation = UnrealLocation;
  LastAppliedUnrealRotation = UnrealRotation;
  bHasAppliedImmutableSnapshot = true;
}

bool AFlyingCoreSimAircraftActor::DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot() const
{
  return bHasAppliedImmutableSnapshot &&
         SceneRoot && GetRootComponent() == SceneRoot &&
         AircraftMesh && IsAttachedTo(AircraftMesh, SceneRoot) &&
         CockpitRoot && IsAttachedTo(CockpitRoot, SceneRoot) &&
         CockpitCamera && IsAttachedTo(CockpitCamera, SceneRoot) &&
         InstrumentCamera && IsAttachedTo(InstrumentCamera, SceneRoot) &&
         ExteriorSpringArm && IsAttachedTo(ExteriorSpringArm, SceneRoot) &&
         ExteriorCamera && IsAttachedTo(ExteriorCamera, SceneRoot);
}

bool AFlyingCoreSimAircraftActor::DoesVisualPresentationMatchImmutableSnapshot(
  const FFlyingCoreSimImmutableStateSnapshot& Snapshot) const
{
  return bHasAppliedImmutableSnapshot &&
         Snapshot.bValid &&
         SnapshotsMatch(LastAppliedImmutableSnapshot, Snapshot) &&
         GetActorLocation().Equals(LastAppliedUnrealLocation, KINDA_SMALL_NUMBER) &&
         GetActorRotation().Equals(LastAppliedUnrealRotation, KINDA_SMALL_NUMBER) &&
         DoCockpitAndExternalVisualsShareAuthoritativeSnapshotRoot();
}

void AFlyingCoreSimAircraftActor::InteractCockpitControl(
  EFlyingCockpitControl Control,
  double PositionNorm,
  bool bEngaged)
{
  if (FFlyingCockpitControlState* State = FindControlState(Control))
  {
    State->PositionNorm = FMath::Clamp(PositionNorm, 0.0, 1.0);
    State->bEngaged = bEngaged;
  }

  if (!CoreSimComponent)
  {
    return;
  }

  switch (Control)
  {
  case EFlyingCockpitControl::BatteryMaster:
    CoreSimComponent->SetAircraftSystemSwitch(TEXT("battery_master"), bEngaged);
    break;
  case EFlyingCockpitControl::Alternator:
    CoreSimComponent->SetAircraftSystemSwitch(TEXT("alternator"), bEngaged);
    break;
  case EFlyingCockpitControl::AvionicsMaster:
    CoreSimComponent->SetAircraftSystemSwitch(TEXT("avionics_master"), bEngaged);
    break;
  case EFlyingCockpitControl::FuelPump:
    CoreSimComponent->SetAircraftSystemSwitch(TEXT("electric_fuel_pump"), bEngaged);
    break;
  case EFlyingCockpitControl::PitotHeat:
    CoreSimComponent->SetAircraftSystemSwitch(TEXT("pitot_heat"), bEngaged);
    break;
  case EFlyingCockpitControl::StandbyVacuum:
    CoreSimComponent->SetAircraftSystemSwitch(TEXT("standby_vacuum_pump"), bEngaged);
    break;
  case EFlyingCockpitControl::FuelSelector:
    CoreSimComponent->SetAircraftFuelSelector(
      PositionNorm < 0.25 ? TEXT("left") : PositionNorm < 0.55 ? TEXT("right") : TEXT("both"));
    break;
  case EFlyingCockpitControl::EmergencyFuelCutoff:
    CoreSimComponent->SetAircraftFuelSelector(bEngaged ? TEXT("off") : TEXT("both"));
    break;
  case EFlyingCockpitControl::FireExtinguisher:
    CoreSimComponent->SetAircraftFailure(TEXT("engine_sensor_power"), bEngaged);
    break;
  case EFlyingCockpitControl::ReplayScrub:
    SetCockpitCameraMode(EFlyingCockpitCameraMode::ReplayInspection);
    CoreSimComponent->ScrubTelemetryReplayNormalized(PositionNorm);
    break;
  default:
    break;
  }

  CoreSimComponent->ApplyMappedAircraftControls(
    BuildMappedInputStateFromCockpit(),
    IsCockpitEngineRunning(CoreSimComponent->GetCurrentInstrumentSnapshot()));
}

void AFlyingCoreSimAircraftActor::SetCockpitNightLighting(bool bEnabled)
{
  bNightLightingEnabled = bEnabled;
  UpdateLighting(CoreSimComponent ? CoreSimComponent->GetCurrentInstrumentSnapshot()
                                  : FFlyingAircraftInstrumentSnapshot{});
}

void AFlyingCoreSimAircraftActor::SetCockpitCameraMode(EFlyingCockpitCameraMode Mode)
{
  ActiveCameraMode = Mode;
  UpdateCamera();
}

TArray<FFlyingCockpitControlState> AFlyingCoreSimAircraftActor::GetCockpitControls() const
{
  return CockpitControls;
}

EFlyingCockpitCameraMode AFlyingCoreSimAircraftActor::GetCockpitCameraMode() const
{
  return ActiveCameraMode;
}

UCameraComponent* AFlyingCoreSimAircraftActor::GetActiveCameraComponent() const
{
  switch (ActiveCameraMode)
  {
  case EFlyingCockpitCameraMode::Pilot:
    return CockpitCamera;
  case EFlyingCockpitCameraMode::Instruments:
    return InstrumentCamera;
  case EFlyingCockpitCameraMode::ExteriorOrbit:
    return ExteriorCamera;
  case EFlyingCockpitCameraMode::ReplayInspection:
    return ReplayInspectionCamera;
  }
  return CockpitCamera;
}

UStaticMeshComponent* AFlyingCoreSimAircraftActor::AddPrimitiveMesh(
  FName Name,
  const TCHAR* MeshPath,
  USceneComponent* Parent,
  FVector RelativeLocation,
  FVector RelativeScale,
  FRotator RelativeRotation,
  FLinearColor Color)
{
  UStaticMeshComponent* Mesh =
    CreateDefaultSubobject<UStaticMeshComponent>(Name);
  Mesh->SetupAttachment(Parent);
  Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Mesh->SetRelativeLocation(RelativeLocation);
  Mesh->SetRelativeScale3D(RelativeScale);
  Mesh->SetRelativeRotation(RelativeRotation);

  if (UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, MeshPath))
  {
    Mesh->SetStaticMesh(StaticMesh);
  }
  if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, kBasicMaterialPath))
  {
    Mesh->SetMaterial(0, Material);
    if (UMaterialInstanceDynamic* DynamicMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0))
    {
      DynamicMaterial->SetVectorParameterValue(TEXT("Color"), Color);
    }
  }
  return Mesh;
}

UTextRenderComponent* AFlyingCoreSimAircraftActor::AddLabel(
  FName Name,
  USceneComponent* Parent,
  const FString& Text,
  FVector RelativeLocation,
  FRotator RelativeRotation,
  float Size,
  FLinearColor Color)
{
  UTextRenderComponent* Label = CreateDefaultSubobject<UTextRenderComponent>(Name);
  Label->SetupAttachment(Parent);
  Label->SetText(FText::FromString(Text));
  Label->SetHorizontalAlignment(EHTA_Center);
  Label->SetVerticalAlignment(EVRTA_TextCenter);
  Label->SetRelativeLocation(RelativeLocation);
  Label->SetRelativeRotation(RelativeRotation);
  Label->SetWorldSize(Size);
  Label->SetTextRenderColor(Color.ToFColor(true));
  return Label;
}

void AFlyingCoreSimAircraftActor::BuildCockpitControls()
{
  struct FControlSpec
  {
    EFlyingCockpitControl Control;
    const TCHAR* Label;
    FVector Location;
    bool bDefaultEngaged;
    double DefaultPosition;
  };

  const FControlSpec Specs[] = {
    {EFlyingCockpitControl::BatteryMaster, TEXT("BAT"), FVector(16.0, -44.0, -16.0), true, 1.0},
    {EFlyingCockpitControl::Alternator, TEXT("ALT"), FVector(16.0, -32.0, -16.0), true, 1.0},
    {EFlyingCockpitControl::AvionicsMaster, TEXT("AVN"), FVector(16.0, -20.0, -16.0), true, 1.0},
    {EFlyingCockpitControl::FuelPump, TEXT("PUMP"), FVector(16.0, -8.0, -16.0), false, 0.0},
    {EFlyingCockpitControl::PitotHeat, TEXT("PITOT"), FVector(16.0, 4.0, -16.0), false, 0.0},
    {EFlyingCockpitControl::StandbyVacuum, TEXT("VAC"), FVector(16.0, 16.0, -16.0), false, 0.0},
    {EFlyingCockpitControl::Magnetos, TEXT("MAG"), FVector(12.0, 31.0, -15.0), true, 1.0},
    {EFlyingCockpitControl::Starter, TEXT("START"), FVector(12.0, 44.0, -15.0), false, 0.0},
    {EFlyingCockpitControl::Throttle, TEXT("THR"), FVector(-16.0, -35.0, -28.0), true, 0.35},
    {EFlyingCockpitControl::Mixture, TEXT("MIX"), FVector(-16.0, -18.0, -28.0), true, 1.0},
    {EFlyingCockpitControl::Propeller, TEXT("PROP"), FVector(-16.0, -1.0, -28.0), true, 1.0},
    {EFlyingCockpitControl::FuelSelector, TEXT("FUEL"), FVector(-12.0, 19.0, -29.0), true, 1.0},
    {EFlyingCockpitControl::ParkingBrake, TEXT("PARK"), FVector(-10.0, 37.0, -29.0), true, 1.0},
    {EFlyingCockpitControl::Flaps, TEXT("FLAP"), FVector(-32.0, 46.0, -31.0), false, 0.0},
    {EFlyingCockpitControl::Trim, TEXT("TRIM"), FVector(-32.0, 30.0, -31.0), false, 0.5},
    {EFlyingCockpitControl::EmergencyFuelCutoff, TEXT("CUT"), FVector(22.0, 57.0, -16.0), false, 0.0},
    {EFlyingCockpitControl::FireExtinguisher, TEXT("FIRE"), FVector(7.0, 57.0, -16.0), false, 0.0},
    {EFlyingCockpitControl::ReplayScrub, TEXT("RPLY"), FVector(-42.0, -48.0, -30.0), false, 0.0},
  };

  for (const FControlSpec& Spec : Specs)
  {
    FFlyingCockpitControlState ControlState;
    ControlState.Control = Spec.Control;
    ControlState.Label = Spec.Label;
    ControlState.PositionNorm = Spec.DefaultPosition;
    ControlState.bEngaged = Spec.bDefaultEngaged;
    CockpitControls.Add(ControlState);
    const FString MeshName = FString::Printf(TEXT("Control_%s"), Spec.Label);
    UStaticMeshComponent* Mesh = AddPrimitiveMesh(
      FName(*MeshName),
      Spec.Control == EFlyingCockpitControl::Starter ? kCylinderMeshPath : kCubeMeshPath,
      CockpitRoot,
      Spec.Location,
      FVector(0.055, 0.055, 0.11),
      FRotator(0.0, 0.0, Spec.DefaultPosition * 35.0),
      Spec.bDefaultEngaged ? FLinearColor(0.05, 0.55, 0.18) : FLinearColor(0.55, 0.06, 0.04));
    CockpitControlMeshes.Add(Mesh);
    const FString LabelName = FString::Printf(TEXT("ControlLabel_%s"), Spec.Label);
    CockpitControlLabels.Add(AddLabel(
      FName(*LabelName),
      CockpitRoot,
      Spec.Label,
      Spec.Location + FVector(5.5, 0.0, 7.5),
      FRotator(0.0, 90.0, 0.0),
      5.0f,
      FLinearColor::White));
  }
}

void AFlyingCoreSimAircraftActor::BuildInstrumentPanel()
{
  struct FInstrumentSpec
  {
    const TCHAR* Name;
    const TCHAR* Label;
    FVector Location;
  };

  const FInstrumentSpec Specs[] = {
    {TEXT("Airspeed"), TEXT("IAS -- kt"), FVector(20.0, -38.0, 10.0)},
    {TEXT("Attitude"), TEXT("ATT --/--"), FVector(20.0, -13.0, 10.0)},
    {TEXT("Altitude"), TEXT("ALT -- ft"), FVector(20.0, 12.0, 10.0)},
    {TEXT("Heading"), TEXT("HDG ---"), FVector(20.0, 37.0, 10.0)},
    {TEXT("VerticalSpeed"), TEXT("VSI -- fpm"), FVector(20.0, -38.0, -7.0)},
    {TEXT("Engine"), TEXT("RPM ----"), FVector(20.0, -13.0, -7.0)},
    {TEXT("Electrical"), TEXT("ELEC -- V"), FVector(20.0, 12.0, -7.0)},
    {TEXT("FuelVacuum"), TEXT("FUEL/VAC"), FVector(20.0, 37.0, -7.0)},
  };

  for (const FInstrumentSpec& Spec : Specs)
  {
    const FString BezelName = FString::Printf(TEXT("GaugeBezel_%s"), Spec.Name);
    AddPrimitiveMesh(
      FName(*BezelName),
      kCylinderMeshPath,
      CockpitRoot,
      Spec.Location + FVector(0.5, 0.0, 0.0),
      FVector(0.025, 0.13, 0.13),
      FRotator(0.0, 90.0, 0.0),
      FLinearColor(0.01, 0.012, 0.014));
    const FString TextName = FString::Printf(TEXT("GaugeText_%s"), Spec.Name);
    InstrumentReadouts.Add(AddLabel(
      FName(*TextName),
      CockpitRoot,
      Spec.Label,
      Spec.Location,
      FRotator(0.0, 90.0, 0.0),
      4.6f,
      FLinearColor(0.86, 0.95, 1.0)));
  }
}

FFlyingCockpitControlState* AFlyingCoreSimAircraftActor::FindControlState(
  EFlyingCockpitControl Control)
{
  return CockpitControls.FindByPredicate(
    [Control](const FFlyingCockpitControlState& State)
    {
      return State.Control == Control;
    });
}

const FFlyingCockpitControlState* AFlyingCoreSimAircraftActor::FindControlState(
  EFlyingCockpitControl Control) const
{
  return CockpitControls.FindByPredicate(
    [Control](const FFlyingCockpitControlState& State)
    {
      return State.Control == Control;
    });
}

FFlyingMappedInputState AFlyingCoreSimAircraftActor::BuildMappedInputStateFromCockpit() const
{
  FFlyingMappedInputState InputState;
  InputState.ThrottleNorm = ControlPosition(CockpitControls, EFlyingCockpitControl::Throttle, 0.0f);
  InputState.MixtureNorm = ControlPosition(CockpitControls, EFlyingCockpitControl::Mixture, 1.0f);
  InputState.PropellerNorm = ControlPosition(CockpitControls, EFlyingCockpitControl::Propeller, 1.0f);
  InputState.FlapsNorm = ControlPosition(CockpitControls, EFlyingCockpitControl::Flaps, 0.0f);

  const bool bParkingBrake =
    ControlEngaged(CockpitControls, EFlyingCockpitControl::ParkingBrake, false);
  InputState.BrakeLeftNorm = bParkingBrake ? 1.0 : 0.0;
  InputState.BrakeRightNorm = bParkingBrake ? 1.0 : 0.0;

  const double TrimNorm = ControlPosition(CockpitControls, EFlyingCockpitControl::Trim, 0.5f) * 2.0 - 1.0;
  InputState.ElevatorTrimNorm = TrimNorm;
  return InputState;
}

bool AFlyingCoreSimAircraftActor::IsCockpitEngineRunning(
  const FFlyingAircraftInstrumentSnapshot& Instruments) const
{
  const bool bMagnetosOn =
    ControlEngaged(CockpitControls, EFlyingCockpitControl::Magnetos, true);
  const bool bStarterEngaged =
    ControlEngaged(CockpitControls, EFlyingCockpitControl::Starter, false);
  const bool bFuelCutoff =
    ControlEngaged(CockpitControls, EFlyingCockpitControl::EmergencyFuelCutoff, false);
  const float MixtureNorm =
    ControlPosition(CockpitControls, EFlyingCockpitControl::Mixture, 1.0f);
  return bMagnetosOn && !bFuelCutoff && MixtureNorm > 0.05f &&
         (bStarterEngaged || Instruments.Engine.Rpm > 400.0);
}

void AFlyingCoreSimAircraftActor::UpdateCockpitFromInstruments(
  const FFlyingAircraftInstrumentSnapshot& Instruments)
{
  if (InstrumentReadouts.Num() >= 8)
  {
    InstrumentReadouts[0]->SetText(FText::FromString(FString::Printf(
      TEXT("IAS %.0f kt"), Instruments.IndicatedAirspeedMetersPerSecond * 1.94384)));
    InstrumentReadouts[1]->SetText(FText::FromString(FString::Printf(
      TEXT("ATT %.0f/%.0f"), Instruments.AttitudeRollDegrees, Instruments.AttitudePitchDegrees)));
    InstrumentReadouts[2]->SetText(FText::FromString(FString::Printf(
      TEXT("ALT %.0f ft"), Instruments.IndicatedAltitudeMeters * 3.28084)));
    InstrumentReadouts[3]->SetText(FText::FromString(FString::Printf(
      TEXT("HDG %03.0f"), Instruments.MagneticHeadingDegrees)));
    InstrumentReadouts[4]->SetText(FText::FromString(FString::Printf(
      TEXT("VSI %.0f fpm"), Instruments.VerticalSpeedMetersPerSecond * 196.85)));
    InstrumentReadouts[5]->SetText(FText::FromString(FString::Printf(
      TEXT("RPM %.0f\nMAP %.0f"), Instruments.Engine.Rpm, Instruments.Engine.ManifoldPressureKpa)));
    InstrumentReadouts[6]->SetText(FText::FromString(FString::Printf(
      TEXT("BUS %.1f V\nBAT %s"), Instruments.Electrical.BusVoltageVolts,
      *BoolLabel(Instruments.Electrical.bBatteryOnline))));
    InstrumentReadouts[7]->SetText(FText::FromString(FString::Printf(
      TEXT("F %.0f/%.0f kg\nVAC %.1f"), Instruments.Fuel.LeftQuantityKg,
      Instruments.Fuel.RightQuantityKg, Instruments.VacuumSuctionInHg)));
  }

  for (int32 Index = 0; Index < CockpitControls.Num() && Index < CockpitControlMeshes.Num(); ++Index)
  {
    const FFlyingCockpitControlState& State = CockpitControls[Index];
    CockpitControlMeshes[Index]->SetRelativeRotation(
      FRotator(0.0, 0.0, State.PositionNorm * 50.0 - 25.0));
  }

  UpdateLighting(Instruments);
  UpdateAudio(Instruments);
  UpdateCamera();
}

void AFlyingCoreSimAircraftActor::UpdateLighting(
  const FFlyingAircraftInstrumentSnapshot& Instruments)
{
  const bool bPoweredNightPanel = bNightLightingEnabled && Instruments.Electrical.bBatteryOnline;
  if (CockpitFloodLight)
  {
    CockpitFloodLight->SetIntensity(bPoweredNightPanel ? 900.0f : 0.0f);
  }
  if (InstrumentBacklight)
  {
    InstrumentBacklight->SetIntensity(bPoweredNightPanel ? 650.0f : 80.0f);
  }
  const FColor LabelColor =
    bNightLightingEnabled ? FLinearColor(1.0, 0.42, 0.24).ToFColor(true)
                          : FLinearColor(0.88, 0.96, 1.0).ToFColor(true);
  for (const TObjectPtr<UTextRenderComponent>& Label : CockpitControlLabels)
  {
    if (Label)
    {
      Label->SetTextRenderColor(LabelColor);
    }
  }
  for (const TObjectPtr<UTextRenderComponent>& Readout : InstrumentReadouts)
  {
    if (Readout)
    {
      Readout->SetTextRenderColor(LabelColor);
    }
  }
}

void AFlyingCoreSimAircraftActor::UpdateAudio(
  const FFlyingAircraftInstrumentSnapshot& Instruments)
{
  const float RpmNorm = FMath::Clamp(static_cast<float>(Instruments.Engine.Rpm / 2700.0), 0.0f, 1.0f);
  const float AirspeedNorm =
    FMath::Clamp(static_cast<float>(Instruments.IndicatedAirspeedMetersPerSecond / 90.0), 0.0f, 1.0f);
  const float LoadNorm =
    FMath::Clamp(static_cast<float>(Instruments.Engine.ManifoldPressureKpa / 95.0), 0.0f, 1.0f);
  const float MixtureNorm =
    ControlPosition(CockpitControls, EFlyingCockpitControl::Mixture, 1.0f);
  const bool bFailureAudible =
    Instruments.Fuel.bEngineFuelStarved || Instruments.bPitotBlocked || Instruments.bStaticBlocked;

  if (EngineAudio)
  {
    EngineAudio->SetVolumeMultiplier(RpmNorm * (0.4f + 0.6f * LoadNorm) * MixtureNorm);
    EngineAudioPitchMultiplier = 0.55f + RpmNorm * 0.95f;
    EngineAudio->SetPitchMultiplier(EngineAudioPitchMultiplier);
  }
  if (PropellerAudio)
  {
    PropellerAudio->SetVolumeMultiplier(RpmNorm * 0.75f);
    PropellerAudioPitchMultiplier = 0.75f + RpmNorm * 1.25f;
    PropellerAudio->SetPitchMultiplier(PropellerAudioPitchMultiplier);
  }
  if (CabinAudio)
  {
    CabinAudio->SetVolumeMultiplier(0.12f + RpmNorm * 0.22f + AirspeedNorm * 0.18f);
    CabinAudioPitchMultiplier = 0.9f + AirspeedNorm * 0.15f;
    CabinAudio->SetPitchMultiplier(CabinAudioPitchMultiplier);
  }
  if (AirflowAudio)
  {
    AirflowAudio->SetVolumeMultiplier(AirspeedNorm * AirspeedNorm);
    AirflowAudioPitchMultiplier = 0.8f + AirspeedNorm * 0.5f;
    AirflowAudio->SetPitchMultiplier(AirflowAudioPitchMultiplier);
  }
  if (DamageAudio)
  {
    DamageAudio->SetVolumeMultiplier(bFailureAudible ? 0.85f : 0.0f);
    DamageAudioPitchMultiplier = Instruments.Fuel.bEngineFuelStarved ? 0.65f : 1.0f;
    DamageAudio->SetPitchMultiplier(DamageAudioPitchMultiplier);
  }
}

void AFlyingCoreSimAircraftActor::QueueProceduralAudio(float DeltaSeconds)
{
  const auto PitchScaledQueueSeconds = [DeltaSeconds](float PitchMultiplier)
  {
    const float PlaybackRate = FMath::Max(PitchMultiplier, 1.0f);
    return FMath::Clamp(
      DeltaSeconds * PlaybackRate,
      kMinimumProceduralAudioQueueSeconds,
      kMaximumProceduralAudioQueueSeconds);
  };

  QueueProceduralTone(
    EngineSound, 95.0f, 0.42f, PitchScaledQueueSeconds(EngineAudioPitchMultiplier),
    EngineTonePhaseRadians);
  QueueProceduralTone(
    PropellerSound, 145.0f, 0.28f, PitchScaledQueueSeconds(PropellerAudioPitchMultiplier),
    PropellerTonePhaseRadians);
  QueueProceduralTone(
    CabinSound, 55.0f, 0.18f, PitchScaledQueueSeconds(CabinAudioPitchMultiplier),
    CabinTonePhaseRadians);
  QueueProceduralTone(
    AirflowSound, 320.0f, 0.12f, PitchScaledQueueSeconds(AirflowAudioPitchMultiplier),
    AirflowTonePhaseRadians);
  QueueProceduralTone(
    DamageSound, 38.0f, 0.55f, PitchScaledQueueSeconds(DamageAudioPitchMultiplier),
    DamageTonePhaseRadians);
}

void AFlyingCoreSimAircraftActor::UpdateCamera()
{
  if (CockpitCamera)
  {
    CockpitCamera->SetActive(ActiveCameraMode == EFlyingCockpitCameraMode::Pilot);
  }
  if (InstrumentCamera)
  {
    InstrumentCamera->SetActive(ActiveCameraMode == EFlyingCockpitCameraMode::Instruments);
  }
  if (ExteriorCamera)
  {
    ExteriorCamera->SetActive(ActiveCameraMode == EFlyingCockpitCameraMode::ExteriorOrbit);
  }
  if (ReplayInspectionCamera)
  {
    ReplayInspectionCamera->SetActive(ActiveCameraMode == EFlyingCockpitCameraMode::ReplayInspection);
  }
}
