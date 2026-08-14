#include "FlyingCesiumGeoreferenceComponent.h"

#include "CesiumGeoreference.h"
#include "FlyingPresentationSettings.h"

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

    if (const AActor* Owner = GetOwner())
    {
        return ACesiumGeoreference::GetDefaultGeoreferenceForActor(Owner);
    }

    return ACesiumGeoreference::GetDefaultGeoreference(this);
}

void UFlyingCesiumGeoreferenceComponent::ConfigureStartupOrigin() const
{
    ACesiumGeoreference* Georeference = ResolveGeoreference();
    if (!Georeference)
    {
        return;
    }

    const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
    Georeference->SetOriginLongitudeLatitudeHeight(FVector(
        Settings->StartupOriginLongitudeDegrees,
        Settings->StartupOriginLatitudeDegrees,
        Settings->StartupOriginHeightMeters));
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
