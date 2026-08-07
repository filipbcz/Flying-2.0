#include "FlyingPresentation.h"

#include "FlyingBuildMetadata.h"
#include "FlyingPresentationSettings.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "FFlyingPresentationModule"

namespace
{
FString ResolveProjectPath(const FString& RawPath)
{
  FString Resolved = RawPath;
  if (FPaths::IsRelative(Resolved))
  {
    Resolved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Resolved);
  }
  FPaths::NormalizeFilename(Resolved);
  FPaths::CollapseRelativeDirectories(Resolved);
  return Resolved;
}

FString EscapeJsonString(FString Value)
{
  Value.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
  Value.ReplaceInline(TEXT("\""), TEXT("\\\""));
  Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
  Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
  Value.ReplaceInline(TEXT("\t"), TEXT("\\t"));
  return Value;
}

const TCHAR* VerbosityToString(ELogVerbosity::Type Verbosity)
{
  switch (Verbosity)
  {
  case ELogVerbosity::Fatal:
    return TEXT("Fatal");
  case ELogVerbosity::Error:
    return TEXT("Error");
  case ELogVerbosity::Warning:
    return TEXT("Warning");
  case ELogVerbosity::Display:
    return TEXT("Display");
  case ELogVerbosity::Log:
    return TEXT("Log");
  case ELogVerbosity::Verbose:
    return TEXT("Verbose");
  case ELogVerbosity::VeryVerbose:
    return TEXT("VeryVerbose");
  default:
    return TEXT("Unknown");
  }
}

class FFlyingStructuredLogOutput final : public FOutputDevice
{
public:
  explicit FFlyingStructuredLogOutput(const FString& InLogPath)
    : LogPath(ResolveProjectPath(InLogPath))
  {
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(LogPath), true);
  }

  void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
  {
    const FFlyingBuildMetadata Metadata = UFlyingBuildMetadata::GetBuildMetadata();
    const FString Line = FString::Printf(
      TEXT("{\"schema\":\"flying.structured-log.v1\",\"timestampUtc\":\"%s\",\"buildId\":\"%s\",\"version\":\"%s\",\"category\":\"%s\",\"verbosity\":\"%s\",\"message\":\"%s\",\"personalTelemetry\":false}\n"),
      *FDateTime::UtcNow().ToIso8601(),
      *EscapeJsonString(Metadata.BuildId),
      *EscapeJsonString(Metadata.Version),
      *EscapeJsonString(Category.ToString()),
      *EscapeJsonString(VerbosityToString(Verbosity)),
      *EscapeJsonString(Message ? FString(Message) : FString()));
    FFileHelper::SaveStringToFile(Line, *LogPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append);
  }

private:
  FString LogPath;
};

TUniquePtr<FFlyingStructuredLogOutput> GStructuredLogOutput;
}

void FFlyingPresentationModule::StartupModule()
{
  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  const FFlyingBuildMetadata Metadata = UFlyingBuildMetadata::GetBuildMetadata();

  FGenericCrashContext::SetGameData(TEXT("FlyingBuildId"), Metadata.BuildId);
  FGenericCrashContext::SetGameData(TEXT("FlyingBuildVersion"), Metadata.Version);
  FGenericCrashContext::SetGameData(TEXT("FlyingBuildChannel"), Metadata.Channel);
  FGenericCrashContext::SetGameData(TEXT("FlyingCrashTelemetryOptIn"), Settings->bCrashTelemetryOptIn ? TEXT("true") : TEXT("false"));
  FGenericCrashContext::SetGameData(TEXT("FlyingStructuredLogPath"), ResolveProjectPath(Settings->StructuredLogPath));
  FGenericCrashContext::SetGameData(TEXT("FlyingCrashDiagnosticsDirectory"), ResolveProjectPath(Settings->CrashDiagnosticsDirectory));

  IFileManager::Get().MakeDirectory(*ResolveProjectPath(Settings->CrashDiagnosticsDirectory), true);
  GStructuredLogOutput = MakeUnique<FFlyingStructuredLogOutput>(Settings->StructuredLogPath);
  if (GLog)
  {
    GLog->AddOutputDevice(GStructuredLogOutput.Get());
  }
  UE_LOG(LogTemp, Display, TEXT("Flying build started: buildId=%s version=%s crashTelemetryOptIn=%s"),
    *Metadata.BuildId,
    *Metadata.Version,
    Settings->bCrashTelemetryOptIn ? TEXT("true") : TEXT("false"));
}

void FFlyingPresentationModule::ShutdownModule()
{
  if (GLog && GStructuredLogOutput)
  {
    GLog->RemoveOutputDevice(GStructuredLogOutput.Get());
  }
  GStructuredLogOutput.Reset();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFlyingPresentationModule, FlyingPresentation)
