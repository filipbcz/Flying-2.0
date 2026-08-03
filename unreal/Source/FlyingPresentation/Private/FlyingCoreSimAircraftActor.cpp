#include "FlyingCoreSimAircraftActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "FlyingCesiumGeoreferenceComponent.h"
#include "FlyingCoreSimComponent.h"
#include "UObject/ConstructorHelpers.h"

AFlyingCoreSimAircraftActor::AFlyingCoreSimAircraftActor()
{
  PrimaryActorTick.bCanEverTick = true;

  SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
  RootComponent = SceneRoot;

  AircraftMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AircraftMesh"));
  AircraftMesh->SetupAttachment(SceneRoot);
  AircraftMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  AircraftMesh->SetRelativeScale3D(FVector(1.8, 0.8, 0.35));

  static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(
    TEXT("/Engine/BasicShapes/Cone.Cone"));
  if (ConeMesh.Succeeded())
  {
    AircraftMesh->SetStaticMesh(ConeMesh.Object);
    AircraftMesh->SetRelativeRotation(FRotator(0.0, 90.0, 0.0));
  }

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

  UpdatePresentationFromSnapshot(CoreSimComponent->GetCurrentSnapshot());
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
