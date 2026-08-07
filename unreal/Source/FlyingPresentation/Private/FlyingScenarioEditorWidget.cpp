#include "FlyingScenarioEditorWidget.h"

#include "FlyingCoreSimComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Guid.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const TCHAR* kScenarioSchema = TEXT("flying.user-scenario.v1");
const TCHAR* kSettingsSchema = TEXT("flying.user-settings.v1");

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

bool SaveJsonAtomic(const FString& RawPath,
                    const TSharedRef<FJsonObject>& Root,
                    FString& OutStatus)
{
  const FString Path = ResolveProjectPath(RawPath);
  if (Path.IsEmpty())
  {
    OutStatus = TEXT("Save path is empty");
    return false;
  }

  FString Json;
  const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
  if (!FJsonSerializer::Serialize(Root, Writer))
  {
    OutStatus = TEXT("Save data could not be serialized");
    return false;
  }

  IFileManager& Files = IFileManager::Get();
  Files.MakeDirectory(*FPaths::GetPath(Path), true);
  const FString TempPath = FString::Printf(
    TEXT("%s.%s.tmp"),
    *Path,
    *FGuid::NewGuid().ToString(EGuidFormats::Digits));
  if (!FFileHelper::SaveStringToFile(Json, *TempPath))
  {
    OutStatus = TEXT("Temporary save file could not be written");
    return false;
  }

  if (!Files.Move(*Path, *TempPath, true, true))
  {
    Files.Delete(*TempPath, false, true);
    OutStatus = TEXT("Save file could not be atomically replaced");
    return false;
  }

  OutStatus = TEXT("Save file written");
  return true;
}

bool LoadJsonRecovering(const FString& RawPath,
                        const TCHAR* ExpectedSchema,
                        TSharedPtr<FJsonObject>& OutRoot,
                        FString& OutStatus)
{
  const FString Path = ResolveProjectPath(RawPath);
  FString Json;
  if (Path.IsEmpty() || !FFileHelper::LoadFileToString(Json, *Path))
  {
    OutStatus = TEXT("Save file is missing");
    return false;
  }

  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
  if (!FJsonSerializer::Deserialize(Reader, OutRoot) || !OutRoot.IsValid())
  {
    IFileManager::Get().Move(*(Path + TEXT(".corrupt")), *Path, true, true);
    OutStatus = TEXT("Save file was corrupt and was moved aside");
    return false;
  }

  FString Schema;
  if (!OutRoot->TryGetStringField(TEXT("schemaVersion"), Schema) || Schema != ExpectedSchema)
  {
    IFileManager::Get().Move(*(Path + TEXT(".unsupported")), *Path, true, true);
    OutStatus = TEXT("Save file schema is unsupported and was moved aside");
    return false;
  }
  return true;
}

FString StartModeToString(EFlyingScenarioStartMode Value)
{
  switch (Value)
  {
  case EFlyingScenarioStartMode::ColdAndDark:
    return TEXT("cold_and_dark");
  case EFlyingScenarioStartMode::Airborne:
    return TEXT("airborne");
  case EFlyingScenarioStartMode::ReadyToTaxi:
  default:
    return TEXT("ready_to_taxi");
  }
}

EFlyingScenarioStartMode StartModeFromString(const FString& Value)
{
  if (Value == TEXT("cold_and_dark"))
  {
    return EFlyingScenarioStartMode::ColdAndDark;
  }
  if (Value == TEXT("airborne"))
  {
    return EFlyingScenarioStartMode::Airborne;
  }
  return EFlyingScenarioStartMode::ReadyToTaxi;
}

FString PositionModeToString(EFlyingScenarioPositionMode Value)
{
  return Value == EFlyingScenarioPositionMode::GeographicPosition
           ? TEXT("geographic_position")
           : TEXT("airport_or_runway");
}

EFlyingScenarioPositionMode PositionModeFromString(const FString& Value)
{
  return Value == TEXT("geographic_position")
           ? EFlyingScenarioPositionMode::GeographicPosition
           : EFlyingScenarioPositionMode::AirportOrRunway;
}

void WriteWeather(const FFlyingManualWeatherScenario& Weather,
                  const TSharedRef<FJsonObject>& Root)
{
  Root->SetNumberField(TEXT("qnhPa"), Weather.QnhPascal);
  Root->SetNumberField(TEXT("seaLevelTemperatureK"), Weather.SeaLevelTemperatureKelvin);
  Root->SetNumberField(TEXT("relativeHumidityNorm"), Weather.RelativeHumidityNorm);
  Root->SetNumberField(TEXT("visibilityM"), Weather.VisibilityMeters);
  Root->SetNumberField(TEXT("surfaceWindNorthMps"), Weather.SurfaceWindNedMetersPerSecond.X);
  Root->SetNumberField(TEXT("surfaceWindEastMps"), Weather.SurfaceWindNedMetersPerSecond.Y);
  Root->SetNumberField(TEXT("surfaceWindDownMps"), Weather.SurfaceWindNedMetersPerSecond.Z);
  Root->SetNumberField(TEXT("turbulenceIntensityMps"), Weather.TurbulenceIntensityMetersPerSecond);
  Root->SetNumberField(TEXT("cloudBaseM"), Weather.CloudBaseMeters);
  Root->SetNumberField(TEXT("cloudTopM"), Weather.CloudTopMeters);
  Root->SetNumberField(TEXT("cloudCoverageNorm"), Weather.CloudCoverageNorm);
  Root->SetNumberField(TEXT("rainRateMmPerHour"), Weather.RainRateMmPerHour);
  Root->SetNumberField(TEXT("snowRateMmPerHour"), Weather.SnowRateMmPerHour);
  Root->SetNumberField(TEXT("surfaceWetnessNorm"), Weather.SurfaceWetnessNorm);
  Root->SetNumberField(TEXT("icingSeverityNorm"), Weather.IcingSeverityNorm);
}

void ReadWeather(const TSharedPtr<FJsonObject>& Root, FFlyingManualWeatherScenario& Weather)
{
  Root->TryGetNumberField(TEXT("qnhPa"), Weather.QnhPascal);
  Root->TryGetNumberField(TEXT("seaLevelTemperatureK"), Weather.SeaLevelTemperatureKelvin);
  Root->TryGetNumberField(TEXT("relativeHumidityNorm"), Weather.RelativeHumidityNorm);
  Root->TryGetNumberField(TEXT("visibilityM"), Weather.VisibilityMeters);
  Root->TryGetNumberField(TEXT("surfaceWindNorthMps"), Weather.SurfaceWindNedMetersPerSecond.X);
  Root->TryGetNumberField(TEXT("surfaceWindEastMps"), Weather.SurfaceWindNedMetersPerSecond.Y);
  Root->TryGetNumberField(TEXT("surfaceWindDownMps"), Weather.SurfaceWindNedMetersPerSecond.Z);
  Root->TryGetNumberField(TEXT("turbulenceIntensityMps"), Weather.TurbulenceIntensityMetersPerSecond);
  Root->TryGetNumberField(TEXT("cloudBaseM"), Weather.CloudBaseMeters);
  Root->TryGetNumberField(TEXT("cloudTopM"), Weather.CloudTopMeters);
  Root->TryGetNumberField(TEXT("cloudCoverageNorm"), Weather.CloudCoverageNorm);
  Root->TryGetNumberField(TEXT("rainRateMmPerHour"), Weather.RainRateMmPerHour);
  Root->TryGetNumberField(TEXT("snowRateMmPerHour"), Weather.SnowRateMmPerHour);
  Root->TryGetNumberField(TEXT("surfaceWetnessNorm"), Weather.SurfaceWetnessNorm);
  Root->TryGetNumberField(TEXT("icingSeverityNorm"), Weather.IcingSeverityNorm);
}

TSharedRef<FJsonObject> ScenarioToJson(const FFlyingScenarioEditorData& Data,
                                       const TCHAR* Schema)
{
  const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
  Root->SetStringField(TEXT("schemaVersion"), Schema);
  Root->SetStringField(TEXT("scenarioName"), Data.ScenarioName);
  Root->SetStringField(TEXT("aircraftId"), Data.AircraftId);
  Root->SetStringField(TEXT("positionMode"), PositionModeToString(Data.PositionMode));
  Root->SetStringField(TEXT("locationId"), Data.AirportSelection.LocationId.ToString());
  Root->SetStringField(TEXT("startMode"), StartModeToString(Data.AirportSelection.StartMode));
  Root->SetNumberField(TEXT("latitudeDeg"), Data.LatitudeDegrees);
  Root->SetNumberField(TEXT("longitudeDeg"), Data.LongitudeDegrees);
  Root->SetNumberField(TEXT("altitudeM"), Data.AltitudeMeters);
  Root->SetNumberField(TEXT("trueHeadingDeg"), Data.TrueHeadingDegrees);
  Root->SetStringField(TEXT("localDate"), Data.LocalDate.ToString(TEXT("%Y-%m-%d")));
  Root->SetNumberField(TEXT("localTimeSeconds"), Data.LocalTimeSeconds);
  Root->SetNumberField(TEXT("pilotAndPayloadWeightKg"), Data.PilotAndPayloadWeightKg);
  Root->SetNumberField(TEXT("fuelWeightKg"), Data.FuelWeightKg);
  WriteWeather(Data.Weather, Root);

  TArray<TSharedPtr<FJsonValue>> Failures;
  for (const FFlyingScenarioFailureSelection& Failure : Data.Failures)
  {
    const TSharedPtr<FJsonObject> FailureObject = MakeShared<FJsonObject>();
    FailureObject->SetStringField(TEXT("failureId"), Failure.FailureId.ToString());
    FailureObject->SetBoolField(TEXT("failed"), Failure.bFailed);
    Failures.Add(MakeShared<FJsonValueObject>(FailureObject));
  }
  Root->SetArrayField(TEXT("failures"), Failures);
  return Root;
}

bool JsonToScenario(const TSharedPtr<FJsonObject>& Root, FFlyingScenarioEditorData& Data)
{
  Root->TryGetStringField(TEXT("scenarioName"), Data.ScenarioName);
  Root->TryGetStringField(TEXT("aircraftId"), Data.AircraftId);
  FString PositionMode;
  Root->TryGetStringField(TEXT("positionMode"), PositionMode);
  Data.PositionMode = PositionModeFromString(PositionMode);

  FString LocationId;
  Root->TryGetStringField(TEXT("locationId"), LocationId);
  Data.AirportSelection.LocationId = FName(*LocationId);
  FString StartMode;
  Root->TryGetStringField(TEXT("startMode"), StartMode);
  Data.AirportSelection.StartMode = StartModeFromString(StartMode);
  Root->TryGetNumberField(TEXT("latitudeDeg"), Data.LatitudeDegrees);
  Root->TryGetNumberField(TEXT("longitudeDeg"), Data.LongitudeDegrees);
  Root->TryGetNumberField(TEXT("altitudeM"), Data.AltitudeMeters);
  Root->TryGetNumberField(TEXT("trueHeadingDeg"), Data.TrueHeadingDegrees);
  FString LocalDate;
  if (Root->TryGetStringField(TEXT("localDate"), LocalDate))
  {
    FDateTime ParsedDate;
    if (FDateTime::ParseIso8601(*LocalDate, ParsedDate))
    {
      Data.LocalDate = ParsedDate;
    }
  }
  Root->TryGetNumberField(TEXT("localTimeSeconds"), Data.LocalTimeSeconds);
  Root->TryGetNumberField(TEXT("pilotAndPayloadWeightKg"), Data.PilotAndPayloadWeightKg);
  Root->TryGetNumberField(TEXT("fuelWeightKg"), Data.FuelWeightKg);
  ReadWeather(Root, Data.Weather);

  Data.Failures.Reset();
  const TArray<TSharedPtr<FJsonValue>>* FailureValues = nullptr;
  if (Root->TryGetArrayField(TEXT("failures"), FailureValues))
  {
    for (const TSharedPtr<FJsonValue>& FailureValue : *FailureValues)
    {
      const TSharedPtr<FJsonObject> FailureObject =
        FailureValue.IsValid() ? FailureValue->AsObject() : nullptr;
      if (!FailureObject.IsValid())
      {
        continue;
      }
      FString FailureId;
      FFlyingScenarioFailureSelection Failure;
      FailureObject->TryGetStringField(TEXT("failureId"), FailureId);
      FailureObject->TryGetBoolField(TEXT("failed"), Failure.bFailed);
      Failure.FailureId = FName(*FailureId);
      Data.Failures.Add(Failure);
    }
  }
  return true;
}
}

void UFlyingScenarioEditorWidget::AddOrUpdateFailure(FName FailureId, bool bFailed)
{
  for (FFlyingScenarioFailureSelection& Failure : EditedScenario.Failures)
  {
    if (Failure.FailureId == FailureId)
    {
      Failure.bFailed = bFailed;
      return;
    }
  }
  FFlyingScenarioFailureSelection Failure;
  Failure.FailureId = FailureId;
  Failure.bFailed = bFailed;
  EditedScenario.Failures.Add(Failure);
}

bool UFlyingScenarioEditorWidget::ApplyEditedScenario(UFlyingCoreSimComponent* CoreSimComponent)
{
  if (!CoreSimComponent)
  {
    LastStatus = TEXT("CoreSim component is missing");
    return false;
  }

  CoreSimComponent->SetAircraftId(EditedScenario.AircraftId);

  const bool bStarted =
    EditedScenario.PositionMode == EFlyingScenarioPositionMode::AirportOrRunway
      ? CoreSimComponent->StartScenario(EditedScenario.AirportSelection)
      : CoreSimComponent->StartScenarioAtPosition(
          EditedScenario.LatitudeDegrees,
          EditedScenario.LongitudeDegrees,
          EditedScenario.AltitudeMeters,
          EditedScenario.TrueHeadingDegrees,
          EditedScenario.AirportSelection.StartMode);
  if (!bStarted)
  {
    LastStatus = CoreSimComponent->LastTelemetryStatus;
    return false;
  }

  CoreSimComponent->SetManualWeatherScenario(EditedScenario.Weather);
  CoreSimComponent->SetAircraftLoadedWeight(EditedScenario.PilotAndPayloadWeightKg);
  CoreSimComponent->SetAircraftFuelWeight(EditedScenario.FuelWeightKg);
  for (const FFlyingScenarioFailureSelection& Failure : EditedScenario.Failures)
  {
    CoreSimComponent->SetAircraftFailure(Failure.FailureId, Failure.bFailed);
  }

  LastStatus = TEXT("Scenario applied");
  return true;
}

bool UFlyingScenarioEditorWidget::SaveScenario()
{
  return SaveJsonAtomic(ScenarioSavePath, ScenarioToJson(EditedScenario, kScenarioSchema), LastStatus);
}

bool UFlyingScenarioEditorWidget::LoadScenario()
{
  TSharedPtr<FJsonObject> Root;
  if (!LoadJsonRecovering(ScenarioSavePath, kScenarioSchema, Root, LastStatus))
  {
    return false;
  }
  JsonToScenario(Root, EditedScenario);
  LastStatus = TEXT("Scenario loaded");
  return true;
}

bool UFlyingScenarioEditorWidget::SaveSettings()
{
  return SaveJsonAtomic(SettingsSavePath, ScenarioToJson(EditedScenario, kSettingsSchema), LastStatus);
}

bool UFlyingScenarioEditorWidget::LoadSettings()
{
  TSharedPtr<FJsonObject> Root;
  if (!LoadJsonRecovering(SettingsSavePath, kSettingsSchema, Root, LastStatus))
  {
    return false;
  }
  JsonToScenario(Root, EditedScenario);
  LastStatus = TEXT("Settings loaded");
  return true;
}
