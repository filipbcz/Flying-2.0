#include "FlyingBuildMetadata.h"

#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
constexpr const TCHAR* kUnknownBuildId = TEXT("local-dev");

bool TryReadStringField(const TSharedPtr<FJsonObject>& Root, const TCHAR* Field, FString& OutValue)
{
  return Root.IsValid() && Root->TryGetStringField(Field, OutValue) && !OutValue.IsEmpty();
}

FFlyingBuildMetadata LoadBuildMetadata()
{
  FFlyingBuildMetadata Metadata;
  Metadata.BuildId = kUnknownBuildId;
  Metadata.Version = FApp::GetBuildVersion().IsEmpty() ? TEXT("0.0.0-dev") : FApp::GetBuildVersion();
  Metadata.Channel = TEXT("local");
  Metadata.Commit = TEXT("unknown");
  Metadata.BuiltAtUtc = TEXT("unknown");

#if defined(FLYING_BUILD_ID)
  Metadata.BuildId = TEXT(PREPROCESSOR_TO_STRING(FLYING_BUILD_ID));
#endif

  const FString MetadataPath = FPaths::ProjectConfigDir() / TEXT("FlyingBuildMetadata.json");
  FString Json;
  if (!FFileHelper::LoadFileToString(Json, *MetadataPath))
  {
    return Metadata;
  }

  TSharedPtr<FJsonObject> Root;
  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
  if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
  {
    return Metadata;
  }

  TryReadStringField(Root, TEXT("buildId"), Metadata.BuildId);
  TryReadStringField(Root, TEXT("version"), Metadata.Version);
  TryReadStringField(Root, TEXT("channel"), Metadata.Channel);
  TryReadStringField(Root, TEXT("commit"), Metadata.Commit);
  TryReadStringField(Root, TEXT("builtAtUtc"), Metadata.BuiltAtUtc);
  return Metadata;
}
}

FFlyingBuildMetadata UFlyingBuildMetadata::GetBuildMetadata()
{
  static const FFlyingBuildMetadata Cached = LoadBuildMetadata();
  return Cached;
}

FString UFlyingBuildMetadata::GetBuildId()
{
  return GetBuildMetadata().BuildId;
}

FString UFlyingBuildMetadata::GetAboutBuildSummary()
{
  const FFlyingBuildMetadata Metadata = GetBuildMetadata();
  return FString::Printf(
    TEXT("Flying %s (%s) [%s]"),
    *Metadata.Version,
    *Metadata.BuildId,
    *Metadata.Channel);
}
