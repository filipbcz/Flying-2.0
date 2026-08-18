#include "FlyingOfflinePilotTerrainActor.h"

#include "FlyingCesiumGeoreferenceComponent.h"
#include "FlyingPresentationSettings.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ConstructorHelpers.h"
#include "flying/geo_terrain/geodesy.hpp"

namespace {

struct FLocalTerrainSample
{
  double EastMeters = 0.0;
  double NorthMeters = 0.0;
  double EllipsoidalHeightMeters = 0.0;
  FVector NormalEnu = FVector::UpVector;
};

struct FLocalTerrainTile
{
  FString TileId;
  FString Path;
  FBox2D Bounds;
  int32 Rows = 0;
  int32 Cols = 0;
  TArray<FLocalTerrainSample> Samples;
};

struct FOfflineImageryTile
{
  FBox2D Bounds;
  int32 Width = 0;
  int32 Height = 0;
  TArray<FLinearColor> Pixels;
};

constexpr int32 kMaxTerrainTileRows = 4096;
constexpr int32 kMaxTerrainTileCols = 4096;
constexpr int64 kMaxTerrainTileSamples = 4LL * 1024LL * 1024LL;
constexpr int32 kMaxPpmDimensionPx = 4096;
constexpr int64 kMaxPpmPixels = 16LL * 1024LL * 1024LL;
constexpr int32 kMaxPpmMaxValue = 65535;

const TCHAR* const kTerrainPackageSchemaVersion = TEXT("flying.terrain-package.v1");
const TCHAR* const kPilotRegionPackageSchemaVersion = TEXT("flying.pilot-region-package.v1");

FString ResolvePackagePath(const FString& RawPath)
{
  if (RawPath.IsEmpty())
  {
    return {};
  }

  FString Resolved = RawPath;
  if (FPaths::IsRelative(Resolved))
  {
    Resolved = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Resolved);
  }
  FPaths::NormalizeFilename(Resolved);
  FPaths::CollapseRelativeDirectories(Resolved);
  return Resolved;
}

bool HasExpectedSchemaVersion(
  const TSharedPtr<FJsonObject>& Manifest,
  const TCHAR* ExpectedSchemaVersion,
  const TCHAR* PackageLabel)
{
  FString ActualSchemaVersion;
  if (!Manifest.IsValid() ||
      !Manifest->TryGetStringField(TEXT("schemaVersion"), ActualSchemaVersion) ||
      ActualSchemaVersion != ExpectedSchemaVersion)
  {
    UE_LOG(
      LogTemp,
      Warning,
      TEXT("%s schemaVersion must be '%s'."),
      PackageLabel,
      ExpectedSchemaVersion);
    return false;
  }

  return true;
}

bool IsPathInsidePackageRoot(FString PackageRoot, FString CandidatePath)
{
  if (PackageRoot.IsEmpty() || CandidatePath.IsEmpty())
  {
    return false;
  }

  FPaths::NormalizeDirectoryName(PackageRoot);
  FPaths::CollapseRelativeDirectories(PackageRoot);
  FPaths::NormalizeFilename(CandidatePath);
  FPaths::CollapseRelativeDirectories(CandidatePath);

  const FString RootPrefix =
    PackageRoot.EndsWith(TEXT("/")) ? PackageRoot : PackageRoot + TEXT("/");
  return CandidatePath.StartsWith(RootPrefix, ESearchCase::IgnoreCase);
}

bool ResolvePackageAssetPath(
  const FString& PackageRoot,
  const FString& ManifestRelativePath,
  FString& OutPath)
{
  if (ManifestRelativePath.IsEmpty() || !FPaths::IsRelative(ManifestRelativePath))
  {
    UE_LOG(
      LogTemp,
      Warning,
      TEXT("Package asset path must be relative to its package root: %s"),
      *ManifestRelativePath);
    return false;
  }

  const FString ResolvedRoot = ResolvePackagePath(PackageRoot);
  const FString CandidatePath =
    ResolvePackagePath(FPaths::Combine(ResolvedRoot, ManifestRelativePath));
  if (!IsPathInsidePackageRoot(ResolvedRoot, CandidatePath))
  {
    UE_LOG(
      LogTemp,
      Error,
      TEXT("Package asset path escapes its package root: %s"),
      *ManifestRelativePath);
    return false;
  }

  OutPath = CandidatePath;
  return true;
}

bool TryReadBoundedJsonInt(
  const TSharedPtr<FJsonObject>& Object,
  const TCHAR* FieldName,
  int32 MinValue,
  int32 MaxValue,
  int32& OutValue)
{
  double NumberValue = 0.0;
  if (!Object.IsValid() || !Object->TryGetNumberField(FieldName, NumberValue) ||
      NumberValue < static_cast<double>(MinValue) ||
      NumberValue > static_cast<double>(MaxValue))
  {
    return false;
  }

  const int32 IntegerValue = static_cast<int32>(NumberValue);
  if (static_cast<double>(IntegerValue) != NumberValue)
  {
    return false;
  }

  OutValue = IntegerValue;
  return true;
}

bool IsTerrainTileSampleCountWithinBounds(int32 Rows, int32 Cols)
{
  const int64 SampleCount = static_cast<int64>(Rows) * static_cast<int64>(Cols);
  return Rows >= 2 && Cols >= 2 &&
         Rows <= kMaxTerrainTileRows &&
         Cols <= kMaxTerrainTileCols &&
         SampleCount <= kMaxTerrainTileSamples;
}

bool TryReadBoundsFromJson(const TSharedPtr<FJsonObject>& BoundsObject, FBox2D& OutBounds)
{
  if (!BoundsObject.IsValid())
  {
    return false;
  }

  double MinEast = 0.0;
  double MaxEast = 0.0;
  double MinNorth = 0.0;
  double MaxNorth = 0.0;
  if (!BoundsObject->TryGetNumberField(TEXT("minEastM"), MinEast) ||
      !BoundsObject->TryGetNumberField(TEXT("maxEastM"), MaxEast) ||
      !BoundsObject->TryGetNumberField(TEXT("minNorthM"), MinNorth) ||
      !BoundsObject->TryGetNumberField(TEXT("maxNorthM"), MaxNorth))
  {
    return false;
  }

  if (MinEast >= MaxEast || MinNorth >= MaxNorth)
  {
    return false;
  }

  OutBounds = FBox2D(FVector2D(MinEast, MinNorth), FVector2D(MaxEast, MaxNorth));
  return true;
}

bool IsPpmPixelCountWithinBounds(int32 Width, int32 Height)
{
  const int64 PixelCount = static_cast<int64>(Width) * static_cast<int64>(Height);
  return Width > 0 && Height > 0 &&
         Width <= kMaxPpmDimensionPx &&
         Height <= kMaxPpmDimensionPx &&
         PixelCount <= kMaxPpmPixels;
}

bool ReadJsonObject(const FString& FilePath, TSharedPtr<FJsonObject>& OutObject)
{
  FString JsonText;
  if (!FFileHelper::LoadFileToString(JsonText, *FilePath))
  {
    UE_LOG(LogTemp, Warning, TEXT("Failed to read JSON package manifest: %s"), *FilePath);
    return false;
  }

  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
  if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
  {
    UE_LOG(LogTemp, Warning, TEXT("Failed to parse JSON package manifest: %s"), *FilePath);
    return false;
  }

  return true;
}

bool HasNonEmptyJsonArrayField(
  const TSharedPtr<FJsonObject>& Object,
  const TCHAR* FieldName,
  bool bMissingIsDisallowed)
{
  const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
  if (!Object.IsValid() || !Object->TryGetArrayField(FieldName, Values))
  {
    return bMissingIsDisallowed;
  }

  return Values->Num() != 0;
}

bool RuntimeDependencySectionUsesRemoteContent(
  const TSharedPtr<FJsonObject>& RuntimeDependencies,
  bool bRemoteTileServerUrlsRequired)
{
  if (!RuntimeDependencies.IsValid())
  {
    return true;
  }

  bool bRuntimeNetworkRequired = true;
  RuntimeDependencies->TryGetBoolField(
    TEXT("runtimeNetworkRequired"),
    bRuntimeNetworkRequired);
  if (bRuntimeNetworkRequired)
  {
    return true;
  }

  if (HasNonEmptyJsonArrayField(
        RuntimeDependencies,
        TEXT("externalMapApis"),
        true))
  {
    return true;
  }

  if (HasNonEmptyJsonArrayField(
        RuntimeDependencies,
        TEXT("remoteTileServerUrls"),
        bRemoteTileServerUrlsRequired))
  {
    return true;
  }

  return false;
}

bool HasDisallowedTerrainRuntimeDependency(const TSharedPtr<FJsonObject>& TerrainPackage)
{
  if (!TerrainPackage.IsValid())
  {
    return true;
  }

  return RuntimeDependencySectionUsesRemoteContent(
    TerrainPackage->GetObjectField(TEXT("streaming")),
    false);
}

bool HasDisallowedPilotRuntimeDependency(const TSharedPtr<FJsonObject>& PilotPackage)
{
  if (!PilotPackage.IsValid())
  {
    return true;
  }

  if (RuntimeDependencySectionUsesRemoteContent(
        PilotPackage->GetObjectField(TEXT("runtimeDependencies")),
        true))
  {
    return true;
  }

  if (PilotPackage->HasField(TEXT("streaming")) &&
      RuntimeDependencySectionUsesRemoteContent(
        PilotPackage->GetObjectField(TEXT("streaming")),
        true))
  {
    return true;
  }

  return false;
}

bool ParseCsvSampleLine(const FString& Line, FLocalTerrainSample& OutSample)
{
  TArray<FString> Fields;
  Line.ParseIntoArray(Fields, TEXT(","), false);
  if (Fields.Num() < 7)
  {
    return false;
  }

  OutSample.EastMeters = FCString::Atod(*Fields[0]);
  OutSample.NorthMeters = FCString::Atod(*Fields[1]);
  OutSample.EllipsoidalHeightMeters = FCString::Atod(*Fields[2]);
  OutSample.NormalEnu = FVector(
    FCString::Atod(*Fields[4]),
    FCString::Atod(*Fields[5]),
    FCString::Atod(*Fields[6]));
  return true;
}

bool LoadTerrainTileCsv(const FString& PackageRoot, FLocalTerrainTile& Tile)
{
  FString CsvText;
  FString TilePath;
  if (!ResolvePackageAssetPath(PackageRoot, Tile.Path, TilePath) ||
      !IsTerrainTileSampleCountWithinBounds(Tile.Rows, Tile.Cols))
  {
    return false;
  }

  if (!FFileHelper::LoadFileToString(CsvText, *TilePath))
  {
    UE_LOG(LogTemp, Warning, TEXT("Failed to read terrain tile CSV: %s"), *TilePath);
    return false;
  }

  TArray<FString> Lines;
  CsvText.ParseIntoArrayLines(Lines, false);
  const int64 ExpectedSampleCount = static_cast<int64>(Tile.Rows) * static_cast<int64>(Tile.Cols);
  Tile.Samples.Reset();
  Tile.Samples.Reserve(static_cast<int32>(ExpectedSampleCount));

  for (int32 Index = 1; Index < Lines.Num(); ++Index)
  {
    FLocalTerrainSample Sample;
    if (ParseCsvSampleLine(Lines[Index], Sample))
    {
      if (Tile.Samples.Num() >= ExpectedSampleCount)
      {
        UE_LOG(LogTemp, Warning, TEXT("Terrain tile CSV has more samples than declared: %s"), *TilePath);
        return false;
      }
      Tile.Samples.Add(Sample);
    }
  }

  return Tile.Samples.Num() == ExpectedSampleCount;
}

bool ParseTerrainTileMetadata(const TSharedPtr<FJsonObject>& TileObject, FLocalTerrainTile& OutTile)
{
  if (!TileObject.IsValid())
  {
    return false;
  }

  const TSharedPtr<FJsonObject>* BoundsObject = nullptr;
  if (!TileObject->TryGetStringField(TEXT("tileId"), OutTile.TileId) ||
      !TileObject->TryGetStringField(TEXT("path"), OutTile.Path) ||
      !TryReadBoundedJsonInt(TileObject, TEXT("rows"), 2, kMaxTerrainTileRows, OutTile.Rows) ||
      !TryReadBoundedJsonInt(TileObject, TEXT("cols"), 2, kMaxTerrainTileCols, OutTile.Cols) ||
      !TileObject->TryGetObjectField(TEXT("bounds"), BoundsObject) ||
      !BoundsObject ||
      !BoundsObject->IsValid() ||
      !TryReadBoundsFromJson(*BoundsObject, OutTile.Bounds) ||
      !IsTerrainTileSampleCountWithinBounds(OutTile.Rows, OutTile.Cols))
  {
    UE_LOG(LogTemp, Warning, TEXT("Terrain package tile metadata is invalid or exceeds bounds."));
    return false;
  }

  return !OutTile.TileId.IsEmpty() && !OutTile.Path.IsEmpty();
}

bool LoadTerrainTiles(const FString& TerrainManifestPath,
                      int32 RequestedLod,
                      int32 MaxTerrainSectionsPerLoad,
                      int32 MaxTerrainVerticesPerSection,
                      const FVector2D& TerrainStreamingFocusLocalMeters,
                      bool& OutUsedRemoteMapDependencies,
                      TArray<FLocalTerrainTile>& OutTiles)
{
  OutUsedRemoteMapDependencies = true;
  TSharedPtr<FJsonObject> TerrainManifest;
  if (!ReadJsonObject(TerrainManifestPath, TerrainManifest))
  {
    return false;
  }

  if (!HasExpectedSchemaVersion(
        TerrainManifest,
        kTerrainPackageSchemaVersion,
        TEXT("Terrain package")))
  {
    return false;
  }

  OutUsedRemoteMapDependencies = HasDisallowedTerrainRuntimeDependency(TerrainManifest);
  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  if (Settings->bOfflineOnly && OutUsedRemoteMapDependencies)
  {
    UE_LOG(LogTemp, Error, TEXT("Terrain package declares runtime map dependencies."));
    return false;
  }

  const TArray<TSharedPtr<FJsonValue>>* RenderLods = nullptr;
  if (!TerrainManifest->TryGetArrayField(TEXT("renderLods"), RenderLods))
  {
    return false;
  }

  TSharedPtr<FJsonObject> SelectedLod;
  for (const TSharedPtr<FJsonValue>& LodValue : *RenderLods)
  {
    const TSharedPtr<FJsonObject> LodObject = LodValue->AsObject();
    if (LodObject.IsValid() &&
        static_cast<int32>(LodObject->GetIntegerField(TEXT("level"))) == RequestedLod)
    {
      SelectedLod = LodObject;
      break;
    }
  }

  if (!SelectedLod.IsValid() && RenderLods->Num() > 0)
  {
    SelectedLod = (*RenderLods)[0]->AsObject();
  }

  if (!SelectedLod.IsValid())
  {
    return false;
  }

  const FString PackageRoot = FPaths::GetPath(TerrainManifestPath);
  const TArray<TSharedPtr<FJsonValue>>* TileValues = nullptr;
  if (!SelectedLod->TryGetArrayField(TEXT("tiles"), TileValues))
  {
    return false;
  }

  TArray<FLocalTerrainTile> CandidateTiles;
  for (const TSharedPtr<FJsonValue>& TileValue : *TileValues)
  {
    FLocalTerrainTile Tile;
    if (!ParseTerrainTileMetadata(TileValue->AsObject(), Tile))
    {
      return false;
    }

    const int64 TileVertexCount =
      static_cast<int64>(Tile.Rows) * static_cast<int64>(Tile.Cols);
    if (MaxTerrainVerticesPerSection > 0 &&
        TileVertexCount > MaxTerrainVerticesPerSection)
    {
      UE_LOG(
        LogTemp,
        Warning,
        TEXT("Skipping terrain tile %s because it exceeds the per-section vertex budget."),
        *Tile.TileId);
      continue;
    }

    CandidateTiles.Add(MoveTemp(Tile));
  }

  CandidateTiles.Sort(
    [&TerrainStreamingFocusLocalMeters](const FLocalTerrainTile& Left, const FLocalTerrainTile& Right)
    {
      const double LeftDistanceSquared =
        (Left.Bounds.GetCenter() - TerrainStreamingFocusLocalMeters).SizeSquared();
      const double RightDistanceSquared =
        (Right.Bounds.GetCenter() - TerrainStreamingFocusLocalMeters).SizeSquared();
      if (LeftDistanceSquared == RightDistanceSquared)
      {
        return Left.TileId < Right.TileId;
      }
      return LeftDistanceSquared < RightDistanceSquared;
    });

  OutTiles.Reset();
  for (FLocalTerrainTile& Tile : CandidateTiles)
  {
    if (MaxTerrainSectionsPerLoad > 0 &&
        OutTiles.Num() >= MaxTerrainSectionsPerLoad)
    {
      UE_LOG(
        LogTemp,
        Warning,
        TEXT("Terrain section budget reached; route-proximity-prioritized tiles beyond the budget stay unloaded for the high-performance profile."));
      break;
    }

    if (!LoadTerrainTileCsv(PackageRoot, Tile))
    {
      return false;
    }
    OutTiles.Add(MoveTemp(Tile));
  }

  return OutTiles.Num() > 0;
}

bool ReadPpmToken(const FString& Text, int32& Offset, FString& OutToken)
{
  OutToken.Reset();
  while (Offset < Text.Len())
  {
    const TCHAR Character = Text[Offset];
    if (Character == TEXT('#'))
    {
      while (Offset < Text.Len() && Text[Offset] != TEXT('\n'))
      {
        ++Offset;
      }
      continue;
    }
    if (!FChar::IsWhitespace(Character))
    {
      break;
    }
    ++Offset;
  }

  while (Offset < Text.Len() && !FChar::IsWhitespace(Text[Offset]))
  {
    OutToken.AppendChar(Text[Offset]);
    ++Offset;
  }

  return !OutToken.IsEmpty();
}

bool TryParseNonNegativeIntToken(const FString& Token, int32& OutValue)
{
  if (Token.IsEmpty())
  {
    return false;
  }

  int64 ParsedValue = 0;
  for (int32 Index = 0; Index < Token.Len(); ++Index)
  {
    const TCHAR Character = Token[Index];
    if (!FChar::IsDigit(Character))
    {
      return false;
    }

    ParsedValue = ParsedValue * 10 + static_cast<int64>(Character - static_cast<TCHAR>('0'));
    if (ParsedValue > kMaxPpmMaxValue)
    {
      return false;
    }
  }

  OutValue = static_cast<int32>(ParsedValue);
  return true;
}

bool LoadPpmP3(const FString& FilePath, int32& OutWidth, int32& OutHeight, TArray<FLinearColor>& OutPixels)
{
  OutWidth = 0;
  OutHeight = 0;
  OutPixels.Reset();

  FString Text;
  if (!FFileHelper::LoadFileToString(Text, *FilePath))
  {
    return false;
  }

  int32 Offset = 0;
  FString Token;
  if (!ReadPpmToken(Text, Offset, Token) || Token != TEXT("P3"))
  {
    return false;
  }

  if (!ReadPpmToken(Text, Offset, Token))
  {
    return false;
  }
  int32 ParsedWidth = 0;
  if (!TryParseNonNegativeIntToken(Token, ParsedWidth))
  {
    return false;
  }

  if (!ReadPpmToken(Text, Offset, Token))
  {
    return false;
  }
  int32 ParsedHeight = 0;
  if (!TryParseNonNegativeIntToken(Token, ParsedHeight) ||
      !IsPpmPixelCountWithinBounds(ParsedWidth, ParsedHeight))
  {
    return false;
  }

  if (!ReadPpmToken(Text, Offset, Token))
  {
    return false;
  }
  int32 MaxValue = 0;
  if (!TryParseNonNegativeIntToken(Token, MaxValue) ||
      MaxValue <= 0 ||
      MaxValue > kMaxPpmMaxValue)
  {
    return false;
  }

  const int64 PixelCount = static_cast<int64>(ParsedWidth) * static_cast<int64>(ParsedHeight);
  OutPixels.Reserve(static_cast<int32>(PixelCount));
  for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
  {
    FString Red;
    FString Green;
    FString Blue;
    if (!ReadPpmToken(Text, Offset, Red) ||
        !ReadPpmToken(Text, Offset, Green) ||
        !ReadPpmToken(Text, Offset, Blue))
    {
      return false;
    }

    int32 RedValue = 0;
    int32 GreenValue = 0;
    int32 BlueValue = 0;
    if (!TryParseNonNegativeIntToken(Red, RedValue) ||
        !TryParseNonNegativeIntToken(Green, GreenValue) ||
        !TryParseNonNegativeIntToken(Blue, BlueValue) ||
        RedValue > MaxValue ||
        GreenValue > MaxValue ||
        BlueValue > MaxValue)
    {
      return false;
    }

    OutPixels.Add(FLinearColor(
      static_cast<float>(RedValue) / static_cast<float>(MaxValue),
      static_cast<float>(GreenValue) / static_cast<float>(MaxValue),
      static_cast<float>(BlueValue) / static_cast<float>(MaxValue),
      1.0f));
  }

  OutWidth = ParsedWidth;
  OutHeight = ParsedHeight;
  return OutPixels.Num() == PixelCount;
}

bool LoadImageryPackage(
  const FString& PilotPackageManifestPath,
  bool& OutUsedRemoteMapDependencies,
  TArray<FOfflineImageryTile>& OutTiles)
{
  OutUsedRemoteMapDependencies = true;
  TSharedPtr<FJsonObject> PilotPackage;
  if (!ReadJsonObject(PilotPackageManifestPath, PilotPackage))
  {
    return false;
  }

  if (!HasExpectedSchemaVersion(
        PilotPackage,
        kPilotRegionPackageSchemaVersion,
        TEXT("Pilot region package")))
  {
    return false;
  }

  OutUsedRemoteMapDependencies = HasDisallowedPilotRuntimeDependency(PilotPackage);
  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  if (Settings->bOfflineOnly && OutUsedRemoteMapDependencies)
  {
    UE_LOG(LogTemp, Error, TEXT("Pilot imagery package declares runtime map dependencies."));
    return false;
  }

  const TSharedPtr<FJsonObject> Imagery = PilotPackage->GetObjectField(TEXT("imagery"));
  const TArray<TSharedPtr<FJsonValue>>* Lods = nullptr;
  if (!Imagery.IsValid() || !Imagery->TryGetArrayField(TEXT("lods"), Lods) || Lods->Num() == 0)
  {
    return false;
  }

  TSharedPtr<FJsonObject> SelectedLod = (*Lods)[0]->AsObject();
  for (const TSharedPtr<FJsonValue>& LodValue : *Lods)
  {
    const TSharedPtr<FJsonObject> LodObject = LodValue->AsObject();
    if (LodObject.IsValid() && LodObject->GetIntegerField(TEXT("level")) == 0)
    {
      SelectedLod = LodObject;
      break;
    }
  }

  const TArray<TSharedPtr<FJsonValue>>* TileValues = nullptr;
  if (!SelectedLod.IsValid() || !SelectedLod->TryGetArrayField(TEXT("tiles"), TileValues))
  {
    return false;
  }

  const FString PackageRoot = FPaths::GetPath(PilotPackageManifestPath);
  OutTiles.Reset();

  for (const TSharedPtr<FJsonValue>& TileValue : *TileValues)
  {
    const TSharedPtr<FJsonObject> TileObject = TileValue->AsObject();
    if (!TileObject.IsValid())
    {
      return false;
    }

    const TSharedPtr<FJsonObject> FileObject = TileObject->GetObjectField(TEXT("file"));
    if (!FileObject.IsValid())
    {
      return false;
    }

    FString RelativePath;
    int32 ExpectedWidth = 0;
    int32 ExpectedHeight = 0;
    if (!FileObject->TryGetStringField(TEXT("path"), RelativePath) ||
        !TryReadBoundedJsonInt(TileObject, TEXT("widthPx"), 1, kMaxPpmDimensionPx, ExpectedWidth) ||
        !TryReadBoundedJsonInt(TileObject, TEXT("heightPx"), 1, kMaxPpmDimensionPx, ExpectedHeight) ||
        !IsPpmPixelCountWithinBounds(ExpectedWidth, ExpectedHeight))
    {
      UE_LOG(LogTemp, Warning, TEXT("Imagery package tile metadata is invalid or exceeds bounds."));
      return false;
    }

    FString ImagePath;
    if (!ResolvePackageAssetPath(PackageRoot, RelativePath, ImagePath))
    {
      return false;
    }

    FOfflineImageryTile Tile;
    const TSharedPtr<FJsonObject>* BoundsObject = nullptr;
    if (!TileObject->TryGetObjectField(TEXT("bounds"), BoundsObject) ||
        !BoundsObject ||
        !BoundsObject->IsValid() ||
        !TryReadBoundsFromJson(*BoundsObject, Tile.Bounds) ||
        !LoadPpmP3(ImagePath, Tile.Width, Tile.Height, Tile.Pixels) ||
        Tile.Width != ExpectedWidth ||
        Tile.Height != ExpectedHeight)
    {
      UE_LOG(LogTemp, Warning, TEXT("Failed to load imagery tile from local package: %s"), *ImagePath);
      return false;
    }

    OutTiles.Add(MoveTemp(Tile));
  }

  return OutTiles.Num() > 0;
}

FLinearColor SampleImagery(
  const TArray<FOfflineImageryTile>& ImageryTiles,
  double EastMeters,
  double NorthMeters)
{
  for (const FOfflineImageryTile& Tile : ImageryTiles)
  {
    if (EastMeters < Tile.Bounds.Min.X || EastMeters > Tile.Bounds.Max.X ||
        NorthMeters < Tile.Bounds.Min.Y || NorthMeters > Tile.Bounds.Max.Y)
    {
      continue;
    }

    const double U =
      (EastMeters - Tile.Bounds.Min.X) / FMath::Max(1.0, Tile.Bounds.Max.X - Tile.Bounds.Min.X);
    const double V =
      (NorthMeters - Tile.Bounds.Min.Y) / FMath::Max(1.0, Tile.Bounds.Max.Y - Tile.Bounds.Min.Y);
    const int32 X =
      FMath::Clamp(FMath::RoundToInt(U * static_cast<double>(Tile.Width - 1)), 0, Tile.Width - 1);
    const int32 Y = FMath::Clamp(
      FMath::RoundToInt((1.0 - V) * static_cast<double>(Tile.Height - 1)),
      0,
      Tile.Height - 1);
    return Tile.Pixels[Y * Tile.Width + X];
  }

  return FLinearColor(0.25f, 0.40f, 0.32f, 1.0f);
}

FVector EcefFromLocalEnu(
  const flying::geo_terrain::LocalTangentFrame& Frame,
  double EastMeters,
  double NorthMeters,
  double UpMeters)
{
  const flying::geo_terrain::EcefPosition Ecef =
    flying::geo_terrain::ecef_from_enu(
      Frame,
      flying::geo_terrain::EnuVector{EastMeters, NorthMeters, UpMeters});
  return FVector(Ecef.meters.x, Ecef.meters.y, Ecef.meters.z);
}

FVector EcefDirectionFromLocalEnu(
  const flying::geo_terrain::LocalTangentFrame& Frame,
  const FVector& EnuDirection)
{
  const flying::geo_terrain::EcefVector Ecef =
    flying::geo_terrain::ecef_vector_from_enu(
      Frame,
      flying::geo_terrain::EnuVector{EnuDirection.X, EnuDirection.Y, EnuDirection.Z});
  return FVector(Ecef.meters.x, Ecef.meters.y, Ecef.meters.z);
}

} // namespace

AFlyingOfflinePilotTerrainActor::AFlyingOfflinePilotTerrainActor()
{
  PrimaryActorTick.bCanEverTick = false;

  TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
  RootComponent = TerrainMesh;
  TerrainMesh->bUseAsyncCooking = true;

  GeoreferenceComponent =
    CreateDefaultSubobject<UFlyingCesiumGeoreferenceComponent>(TEXT("CesiumGeoreference"));

  static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterialAsset(
    TEXT("/Engine/EngineMaterials/VertexColorMaterial.VertexColorMaterial"));
  if (VertexColorMaterialAsset.Succeeded())
  {
    VertexColorMaterial = VertexColorMaterialAsset.Object;
  }
}

void AFlyingOfflinePilotTerrainActor::BeginPlay()
{
  Super::BeginPlay();

  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  if (TerrainPackageManifestPath.IsEmpty())
  {
    TerrainPackageManifestPath = Settings->TerrainPackageManifestPath;
  }
  if (PilotRegionPackageManifestPath.IsEmpty())
  {
    PilotRegionPackageManifestPath = Settings->PilotRegionPackageManifestPath;
  }
  if (RenderLodLevel < 0)
  {
    RenderLodLevel = Settings->HighGraphicsTerrainLodLevel;
  }
  if (MaxTerrainSectionsPerLoad <= 0)
  {
    MaxTerrainSectionsPerLoad = Settings->MaxTerrainSectionsPerLoad;
  }
  if (MaxTerrainVerticesPerSection <= 0)
  {
    MaxTerrainVerticesPerSection = Settings->MaxTerrainVerticesPerSection;
  }

  GeoreferenceComponent->ConfigureOriginFromSettings();

  if (bLoadOnBeginPlay)
  {
    LoadOfflinePackages();
  }
}

bool AFlyingOfflinePilotTerrainActor::LoadOfflinePackages()
{
  const UFlyingPresentationSettings* Settings = GetDefault<UFlyingPresentationSettings>();
  if (TerrainPackageManifestPath.IsEmpty())
  {
    TerrainPackageManifestPath = Settings->TerrainPackageManifestPath;
  }
  if (PilotRegionPackageManifestPath.IsEmpty())
  {
    PilotRegionPackageManifestPath = Settings->PilotRegionPackageManifestPath;
  }
  if (RenderLodLevel < 0)
  {
    RenderLodLevel = Settings->HighGraphicsTerrainLodLevel;
  }
  if (MaxTerrainSectionsPerLoad <= 0)
  {
    MaxTerrainSectionsPerLoad = Settings->MaxTerrainSectionsPerLoad;
  }
  if (MaxTerrainVerticesPerSection <= 0)
  {
    MaxTerrainVerticesPerSection = Settings->MaxTerrainVerticesPerSection;
  }

  const FString TerrainManifestPath = ResolvePackagePath(TerrainPackageManifestPath);
  const FString PilotManifestPath = ResolvePackagePath(PilotRegionPackageManifestPath);

  LastLoadedTerrainPackageManifestPath = TerrainManifestPath;
  LastLoadedPilotRegionPackageManifestPath = PilotManifestPath;
  LastLoadedTerrainTileCount = 0;
  LastLoadedImageryTileCount = 0;
  LastRenderedVertexCount = 0;
  LastRenderedTriangleCount = 0;
  FirstRenderedEcefPositionMeters = FVector::ZeroVector;
  FirstRenderedUnrealPosition = FVector::ZeroVector;
  bLastLoadUsedRemoteMapDependencies = true;

  TArray<FLocalTerrainTile> TerrainTiles;
  bool bTerrainManifestUsedRemoteMapDependencies = true;
  if (!LoadTerrainTiles(
        TerrainManifestPath,
        RenderLodLevel,
        MaxTerrainSectionsPerLoad,
        MaxTerrainVerticesPerSection,
        TerrainStreamingFocusLocalMeters,
        bTerrainManifestUsedRemoteMapDependencies,
        TerrainTiles))
  {
    UE_LOG(LogTemp, Warning, TEXT("No local terrain package tiles loaded from %s"), *TerrainManifestPath);
    return false;
  }
  LastLoadedTerrainTileCount = TerrainTiles.Num();

  TArray<FOfflineImageryTile> ImageryTiles;
  bool bPilotManifestUsedRemoteMapDependencies = false;
  if (!PilotManifestPath.IsEmpty())
  {
    bPilotManifestUsedRemoteMapDependencies = true;
    if (!LoadImageryPackage(
          PilotManifestPath,
          bPilotManifestUsedRemoteMapDependencies,
          ImageryTiles))
    {
      UE_LOG(LogTemp, Error, TEXT("Configured pilot imagery package failed to load: %s"), *PilotManifestPath);
      return false;
    }
  }
  LastLoadedImageryTileCount = ImageryTiles.Num();
  bLastLoadUsedRemoteMapDependencies =
    bTerrainManifestUsedRemoteMapDependencies ||
    bPilotManifestUsedRemoteMapDependencies;

  const auto Origin = flying::geo_terrain::make_geodetic_degrees(
    Settings->PilotOriginLatitudeDegrees,
    Settings->PilotOriginLongitudeDegrees,
    flying::geo_terrain::EllipsoidalHeight{Settings->PilotOriginHeightMeters});
  const flying::geo_terrain::LocalTangentFrame Frame =
    flying::geo_terrain::make_local_tangent_frame(Origin);

  TerrainMesh->ClearAllMeshSections();
  if (VertexColorMaterial)
  {
    TerrainMesh->SetMaterial(0, VertexColorMaterial);
  }

  int32 SectionIndex = 0;
  bool bCapturedFirstRenderedVertex = false;
  for (const FLocalTerrainTile& Tile : TerrainTiles)
  {
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> Uv0;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;

    Vertices.Reserve(Tile.Samples.Num());
    Normals.Reserve(Tile.Samples.Num());
    Uv0.Reserve(Tile.Samples.Num());
    VertexColors.Reserve(Tile.Samples.Num());
    Tangents.Reserve(Tile.Samples.Num());

    for (int32 Row = 0; Row < Tile.Rows; ++Row)
    {
      for (int32 Col = 0; Col < Tile.Cols; ++Col)
      {
        const FLocalTerrainSample& Sample = Tile.Samples[Row * Tile.Cols + Col];
        const double LocalUpMeters =
          Sample.EllipsoidalHeightMeters - Settings->PilotOriginHeightMeters;
        const FVector EcefPosition = EcefFromLocalEnu(
          Frame,
          Sample.EastMeters,
          Sample.NorthMeters,
          LocalUpMeters);
        const FVector EcefNormal = EcefDirectionFromLocalEnu(Frame, Sample.NormalEnu);
        const FVector UnrealPosition =
          GeoreferenceComponent->TransformEcefPositionToUnreal(EcefPosition);

        if (!bCapturedFirstRenderedVertex)
        {
          FirstRenderedEcefPositionMeters = EcefPosition;
          FirstRenderedUnrealPosition = UnrealPosition;
          bCapturedFirstRenderedVertex = true;
        }

        Vertices.Add(UnrealPosition);
        Normals.Add(GeoreferenceComponent->TransformEcefDirectionToUnreal(EcefNormal).GetSafeNormal());
        Uv0.Add(FVector2D(
          static_cast<double>(Col) / static_cast<double>(FMath::Max(1, Tile.Cols - 1)),
          static_cast<double>(Row) / static_cast<double>(FMath::Max(1, Tile.Rows - 1))));
        VertexColors.Add(SampleImagery(ImageryTiles, Sample.EastMeters, Sample.NorthMeters));
        Tangents.Add(FProcMeshTangent(1.0f, 0.0f, 0.0f));
      }
    }

    for (int32 Row = 0; Row < Tile.Rows - 1; ++Row)
    {
      for (int32 Col = 0; Col < Tile.Cols - 1; ++Col)
      {
        const int32 SouthWest = Row * Tile.Cols + Col;
        const int32 SouthEast = SouthWest + 1;
        const int32 NorthWest = (Row + 1) * Tile.Cols + Col;
        const int32 NorthEast = NorthWest + 1;

        Triangles.Add(SouthWest);
        Triangles.Add(NorthWest);
        Triangles.Add(SouthEast);
        Triangles.Add(SouthEast);
        Triangles.Add(NorthWest);
        Triangles.Add(NorthEast);
      }
    }

    TerrainMesh->CreateMeshSection_LinearColor(
      SectionIndex,
      Vertices,
      Triangles,
      Normals,
      Uv0,
      VertexColors,
      Tangents,
      true);

    LastRenderedVertexCount += Vertices.Num();
    LastRenderedTriangleCount += Triangles.Num() / 3;

    if (VertexColorMaterial)
    {
      TerrainMesh->SetMaterial(SectionIndex, VertexColorMaterial);
    }
    ++SectionIndex;
  }

  return SectionIndex > 0;
}

int32 AFlyingOfflinePilotTerrainActor::GetRenderedTerrainSectionCount() const
{
  return TerrainMesh ? TerrainMesh->GetNumSections() : 0;
}

bool AFlyingOfflinePilotTerrainActor::HasRenderedTerrainSections() const
{
  return GetRenderedTerrainSectionCount() > 0;
}

FString AFlyingOfflinePilotTerrainActor::GetLoadedTerrainPackageManifestPath() const
{
  return LastLoadedTerrainPackageManifestPath;
}

FString AFlyingOfflinePilotTerrainActor::GetLoadedPilotRegionPackageManifestPath() const
{
  return LastLoadedPilotRegionPackageManifestPath;
}

int32 AFlyingOfflinePilotTerrainActor::GetLoadedTerrainTileCount() const
{
  return LastLoadedTerrainTileCount;
}

int32 AFlyingOfflinePilotTerrainActor::GetLoadedImageryTileCount() const
{
  return LastLoadedImageryTileCount;
}

int32 AFlyingOfflinePilotTerrainActor::GetLastRenderedVertexCount() const
{
  return LastRenderedVertexCount;
}

int32 AFlyingOfflinePilotTerrainActor::GetLastRenderedTriangleCount() const
{
  return LastRenderedTriangleCount;
}

FVector AFlyingOfflinePilotTerrainActor::GetFirstRenderedEcefPositionMeters() const
{
  return FirstRenderedEcefPositionMeters;
}

FVector AFlyingOfflinePilotTerrainActor::GetFirstRenderedUnrealPosition() const
{
  return FirstRenderedUnrealPosition;
}

bool AFlyingOfflinePilotTerrainActor::DidLastLoadUseRemoteMapDependencies() const
{
  return bLastLoadUsedRemoteMapDependencies;
}
