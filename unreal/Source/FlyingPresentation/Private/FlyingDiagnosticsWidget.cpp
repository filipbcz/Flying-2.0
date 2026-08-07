#include "FlyingDiagnosticsWidget.h"

#include "FlyingBuildMetadata.h"
#include "FlyingCoreSimComponent.h"
#include "FlyingPresentationSettings.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "flying/core_sim/fixed_step.hpp"
#include "flying/core_sim/telemetry.hpp"
#include "flying/geo_terrain/geodesy.hpp"

#include <string>

namespace
{
FString ResolveProjectPath(const FString& RawPath)
{
  FString Resolved = RawPath;
  if (Resolved.IsEmpty())
  {
    return {};
  }
  if (FPaths::IsRelative(Resolved))
  {
    Resolved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Resolved);
  }
  FPaths::NormalizeFilename(Resolved);
  FPaths::CollapseRelativeDirectories(Resolved);
  return Resolved;
}

FString ReadPackageVersion(const FString& RawPath)
{
  const FString Path = ResolveProjectPath(RawPath);
  FString Json;
  if (!FFileHelper::LoadFileToString(Json, *Path))
  {
    return FString::Printf(TEXT("%s: unavailable"), *FPaths::GetCleanFilename(Path));
  }

  TSharedPtr<FJsonObject> Root;
  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
  if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
  {
    return FString::Printf(TEXT("%s: invalid"), *FPaths::GetCleanFilename(Path));
  }

  FString PackageId;
  FString PackageVersion;
  Root->TryGetStringField(TEXT("packageId"), PackageId);
  Root->TryGetStringField(TEXT("packageVersion"), PackageVersion);
  return FString::Printf(TEXT("%s:%s"), *PackageId, *PackageVersion);
}
}

void UFlyingDiagnosticsWidget::SetDiagnosticsVisible(bool bVisible)
{
  bDiagnosticsVisible = bVisible;
}

bool UFlyingDiagnosticsWidget::RefreshDiagnostics(UFlyingCoreSimComponent* CoreSimComponent)
{
  if (!CoreSimComponent)
  {
    Diagnostics = {};
    return false;
  }

  const FFlyingCoreSimStateSnapshot& Snapshot = CoreSimComponent->GetCurrentSnapshot();
  const FFlyingAircraftInstrumentSnapshot& Instruments =
    CoreSimComponent->GetCurrentInstrumentSnapshot();
  if (!Snapshot.bValid)
  {
    Diagnostics = {};
    return false;
  }

  const flying::geo_terrain::EcefPosition Ecef{
    {Snapshot.EcefPositionMeters.X, Snapshot.EcefPositionMeters.Y, Snapshot.EcefPositionMeters.Z}};
  const flying::geo_terrain::GeodeticCoordinates Geodetic =
    flying::geo_terrain::ecef_to_geodetic(Ecef);
  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();

  Diagnostics.bValid = true;
  Diagnostics.LatitudeDegrees = Geodetic.latitude_degrees();
  Diagnostics.LongitudeDegrees = Geodetic.longitude_degrees();
  Diagnostics.EllipsoidalAltitudeMeters = Geodetic.ellipsoidal_height.meters;
  Diagnostics.IndicatedAltitudeMeters = Instruments.IndicatedAltitudeMeters;
  Diagnostics.AglAltitudeMeters =
    Diagnostics.EllipsoidalAltitudeMeters -
    CoreSimComponent->GetCurrentScenarioState().Location.ElevationMeters;
  Diagnostics.SimulationTimeSeconds = Snapshot.SimulationTimeSeconds;
  Diagnostics.CoreSimStepIndex = Snapshot.StepIndex;
  Diagnostics.StateHash = Snapshot.StateHash;
  Diagnostics.FixedStepSeconds = flying::core_sim::kFixedStepSeconds;
  Diagnostics.TerrainSourceTile = ResolveProjectPath(Settings->TerrainPackageManifestPath);
  Diagnostics.Weather = Snapshot.Weather;
  Diagnostics.InputState = CoreSimComponent->GetLastMappedInputState();
  Diagnostics.AverageFrameRate = CoreSimComponent->GetAverageFrameRate();
  Diagnostics.OnePercentLowFrameRate = CoreSimComponent->GetOnePercentLowFrameRate();
  Diagnostics.LastCoreSimInputProcessingMilliseconds =
    CoreSimComponent->GetLastCoreSimInputProcessingMilliseconds();
  Diagnostics.MaxObservedHitchMilliseconds =
    CoreSimComponent->GetMaxObservedHitchMilliseconds();
  Diagnostics.CoreSimMissedStepCount = CoreSimComponent->GetCoreSimMissedStepCount();
  Diagnostics.MaxCoreSimStepsPerFrame = CoreSimComponent->GetMaxCoreSimStepsPerFrame();
  Diagnostics.RamBudgetGiB = Settings->MaximumSoakRamGiB;
  Diagnostics.VramBudgetGiB = Settings->MaximumSoakVramGiB;
  Diagnostics.BuildVersion = FApp::GetBuildVersion();
  Diagnostics.BuildId = UFlyingBuildMetadata::GetBuildId();
  Diagnostics.AboutBuildSummary = UFlyingBuildMetadata::GetAboutBuildSummary();
  const std::string CoreSimVersion{flying::core_sim::core_sim_version()};
  Diagnostics.CoreSimVersion = FString(UTF8_TO_TCHAR(CoreSimVersion.c_str()));
  Diagnostics.DataVersions = FString::Printf(
    TEXT("%s | %s"),
    *ReadPackageVersion(Settings->TerrainPackageManifestPath),
    *ReadPackageVersion(Settings->PilotRegionPackageManifestPath));
  return true;
}
