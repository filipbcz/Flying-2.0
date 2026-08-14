#include "FlyingStartupGameMode.h"

#include "CesiumGeoreference.h"
#include "FlyingCesiumGeoreferenceComponent.h"
#include "FlyingPresentationSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogFlyingStartupShell, Log, All);

void AFlyingStartupGameMode::BeginPlay()
{
    Super::BeginPlay();

    ACesiumGeoreference* Georeference =
        ACesiumGeoreference::GetDefaultGeoreference(GetWorld());
    if (!Georeference)
    {
        Georeference = GetWorld()->SpawnActor<ACesiumGeoreference>();
    }

    UFlyingCesiumGeoreferenceComponent* PresentationGeoreference =
        NewObject<UFlyingCesiumGeoreferenceComponent>(this);
    PresentationGeoreference->GeoreferenceOverride = Georeference;
    PresentationGeoreference->RegisterComponent();
    PresentationGeoreference->ConfigureStartupOrigin();

    const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
    UE_LOG(
        LogFlyingStartupShell,
        Display,
        TEXT("Flying georeferenced simulator shell initialized: lon=%.8f lat=%.8f height=%.2f offlineOnly=%s"),
        Settings->StartupOriginLongitudeDegrees,
        Settings->StartupOriginLatitudeDegrees,
        Settings->StartupOriginHeightMeters,
        Settings->bOfflineOnly ? TEXT("true") : TEXT("false"));
}
