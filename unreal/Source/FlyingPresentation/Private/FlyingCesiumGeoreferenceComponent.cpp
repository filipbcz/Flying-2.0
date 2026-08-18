#include "FlyingCesiumGeoreferenceComponent.h"

#include "CesiumGeoreference.h"
#include "FlyingPresentationSettings.h"

namespace {

FVector RotatedEcefDirection(const FQuat& BodyToEcef, const FVector& BodyAxis)
{
  return BodyToEcef.RotateVector(BodyAxis).GetSafeNormal();
}

FRotator TransformBodyQuatToUnrealRotator(
  const UFlyingCesiumGeoreferenceComponent& Component,
  const FQuat& BodyToEcef)
{
  const FVector ForwardEcef =
    RotatedEcefDirection(BodyToEcef, FVector::ForwardVector);
  const FVector RightEcef =
    RotatedEcefDirection(BodyToEcef, FVector::RightVector);
  const FVector ForwardUnreal = Component.TransformEcefDirectionToUnreal(ForwardEcef).GetSafeNormal();
  const FVector RightUnreal = Component.TransformEcefDirectionToUnreal(RightEcef).GetSafeNormal();

  if (ForwardUnreal.IsNearlyZero() || RightUnreal.IsNearlyZero())
  {
    return FRotator::ZeroRotator;
  }

  return FRotationMatrix::MakeFromXY(ForwardUnreal, RightUnreal).Rotator();
}

} // namespace

UFlyingCesiumGeoreferenceComponent::UFlyingCesiumGeoreferenceComponent()
{
  PrimaryComponentTick.bCanEverTick = false;
}

ACesiumGeoreference* UFlyingCesiumGeoreferenceComponent::ResolveGeoreference() const
{
  if (GeoreferenceOverride)
  {
    return GeoreferenceOverride;
  }

  if (AActor* Owner = GetOwner())
  {
    return ACesiumGeoreference::GetDefaultGeoreferenceForActor(Owner);
  }

  return ACesiumGeoreference::GetDefaultGeoreference(this);
}

void UFlyingCesiumGeoreferenceComponent::ConfigureOriginFromSettings() const
{
  ACesiumGeoreference* Georeference = ResolveGeoreference();
  if (!Georeference)
  {
    return;
  }

  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  Georeference->SetOriginLongitudeLatitudeHeight(FVector(
    Settings->PilotOriginLongitudeDegrees,
    Settings->PilotOriginLatitudeDegrees,
    Settings->PilotOriginHeightMeters));
}

FVector UFlyingCesiumGeoreferenceComponent::TransformEcefPositionToUnreal(
  const FVector& EcefPositionMeters) const
{
  if (const ACesiumGeoreference* Georeference = ResolveGeoreference())
  {
    return Georeference->TransformEarthCenteredEarthFixedPositionToUnreal(EcefPositionMeters);
  }

  return FVector::ZeroVector;
}

FVector UFlyingCesiumGeoreferenceComponent::TransformEcefDirectionToUnreal(
  const FVector& EcefDirection) const
{
  if (const ACesiumGeoreference* Georeference = ResolveGeoreference())
  {
    return Georeference->TransformEarthCenteredEarthFixedDirectionToUnreal(EcefDirection);
  }

  return FVector::ZeroVector;
}

FRotator UFlyingCesiumGeoreferenceComponent::TransformBodyToUnrealRotator(
  const FFlyingCoreSimStateSnapshot& Snapshot) const
{
  if (!Snapshot.bValid)
  {
    return FRotator::ZeroRotator;
  }

  return TransformBodyQuatToUnrealRotator(*this, Snapshot.BodyToEcef);
}

FRotator UFlyingCesiumGeoreferenceComponent::TransformBodyToUnrealRotator(
  const FFlyingCoreSimImmutableStateSnapshot& Snapshot) const
{
  if (!Snapshot.bValid)
  {
    return FRotator::ZeroRotator;
  }

  return TransformBodyQuatToUnrealRotator(*this, Snapshot.BodyToEcef);
}
