#include "FlyingOfflineNavigationMapWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Containers/StringConv.h"
#include "FlyingPresentationSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SQLiteDatabase.h"

namespace
{
constexpr TCHAR kRequiredLayers[][24] = {
  TEXT("zabaged-base"),
  TEXT("geonames-labels"),
  TEXT("airports"),
  TEXT("runways"),
  TEXT("obstacles"),
  TEXT("airspaces"),
};

FString ResolveProjectPath(const FString& RawPath)
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

bool IsLocalTileArchivePath(const FString& Path)
{
  const FString Lower = Path.ToLower();
  return !Path.IsEmpty() && !Lower.Contains(TEXT("://")) &&
         !Lower.StartsWith(TEXT("//")) &&
         (Lower.EndsWith(TEXT(".pmtiles")) || Lower.EndsWith(TEXT(".mbtiles")));
}

bool DoesFormatMatchArchivePath(const FString& Format, const FString& Path)
{
  const FString LowerFormat = Format.ToLower();
  const FString LowerPath = Path.ToLower();
  return (LowerFormat == TEXT("pmtiles") && LowerPath.EndsWith(TEXT(".pmtiles"))) ||
         (LowerFormat == TEXT("mbtiles") && LowerPath.EndsWith(TEXT(".mbtiles")));
}

bool HasExpectedArchiveMagic(const FString& Format, const FString& ResolvedPath)
{
  TArray<uint8> Header;
  if (!FFileHelper::LoadFileToArray(Header, *ResolvedPath) || Header.Num() < 8)
  {
    return false;
  }

  const FString LowerFormat = Format.ToLower();
  if (LowerFormat == TEXT("pmtiles"))
  {
    return Header[0] == 'P' && Header[1] == 'M' && Header[2] == 'T' &&
           Header[3] == 'i' && Header[4] == 'l' && Header[5] == 'e' &&
           Header[6] == 's' && Header[7] >= 3;
  }
  if (LowerFormat == TEXT("mbtiles"))
  {
    return Header.Num() >= 16 && Header[0] == 'S' && Header[1] == 'Q' &&
           Header[2] == 'L' && Header[3] == 'i' && Header[4] == 't' &&
           Header[5] == 'e' && Header[6] == ' ' && Header[7] == 'f' &&
           Header[8] == 'o' && Header[9] == 'r' && Header[10] == 'm' &&
           Header[11] == 'a' && Header[12] == 't' && Header[13] == ' ' &&
           Header[14] == '3' && Header[15] == 0;
  }
  return false;
}

EFlyingNavigationMapLayer LayerForPackageId(const FString& LayerId)
{
  if (LayerId == TEXT("airports"))
  {
    return EFlyingNavigationMapLayer::Airports;
  }
  if (LayerId == TEXT("runways"))
  {
    return EFlyingNavigationMapLayer::Runways;
  }
  if (LayerId == TEXT("obstacles"))
  {
    return EFlyingNavigationMapLayer::Obstacles;
  }
  if (LayerId == TEXT("airspaces"))
  {
    return EFlyingNavigationMapLayer::Airspaces;
  }
  if (LayerId == TEXT("geonames-labels"))
  {
    return EFlyingNavigationMapLayer::Labels;
  }
  return EFlyingNavigationMapLayer::Airspaces;
}

bool HasRemoteToken(const FString& Value)
{
  const FString Lower = Value.ToLower();
  return Lower.Contains(TEXT("://")) || Lower.Contains(TEXT("apikey")) ||
         Lower.Contains(TEXT("api_key")) || Lower.Contains(TEXT("access_token")) ||
         Lower.Contains(TEXT("mapbox"));
}

FVector2D ToCanvasPoint(const FFlyingNavigationMapOverlayPoint& Point)
{
  return FVector2D(Point.EastMeters * 0.02, -Point.NorthMeters * 0.02);
}

FVector2D TilePoint(double EastMeters, double NorthMeters)
{
  return FVector2D(EastMeters * 0.02, -NorthMeters * 0.02);
}

uint64 ReadUint64Le(const TArray<uint8>& Bytes, int32 Offset)
{
  if (Offset < 0 || Offset + 8 > Bytes.Num())
  {
    return 0;
  }

  uint64 Value = 0;
  for (int32 Index = 0; Index < 8; ++Index)
  {
    Value |= static_cast<uint64>(Bytes[Offset + Index]) << (Index * 8);
  }
  return Value;
}

bool LoadPmtilesTilePayload(const FString& ResolvedPath, FString& OutPayload)
{
  TArray<uint8> Archive;
  if (!FFileHelper::LoadFileToArray(Archive, *ResolvedPath) || Archive.Num() < 127)
  {
    return false;
  }

  const uint64 TileDataOffset = ReadUint64Le(Archive, 56);
  const uint64 TileDataLength = ReadUint64Le(Archive, 64);
  if (TileDataOffset == 0 || TileDataLength == 0 ||
      TileDataOffset > static_cast<uint64>(Archive.Num()) ||
      TileDataLength > static_cast<uint64>(Archive.Num()) - TileDataOffset)
  {
    return false;
  }

  const auto* TileBytes =
    reinterpret_cast<const ANSICHAR*>(Archive.GetData() + static_cast<int32>(TileDataOffset));
  FUTF8ToTCHAR Converted(TileBytes, static_cast<int32>(TileDataLength));
  OutPayload = FString(Converted.Length(), Converted.Get());
  return true;
}

bool LoadMbtilesTilePayload(const FString& ResolvedPath, FString& OutPayload)
{
  FSQLiteDatabase Database;
  if (!Database.Open(*ResolvedPath, ESQLiteDatabaseOpenMode::ReadOnly))
  {
    return false;
  }

  FSQLitePreparedStatement Statement;
  const TCHAR* Query =
    TEXT("SELECT tile_data FROM tiles ORDER BY zoom_level, tile_column, tile_row LIMIT 1");
  if (!Statement.Create(Database, Query, ESQLitePreparedStatementFlags::Persistent))
  {
    Database.Close();
    return false;
  }

  TArray<uint8> TileBytes;
  const bool bHasTile =
    Statement.Step() == ESQLitePreparedStatementStepResult::Row &&
    Statement.GetColumnValueByIndex(0, TileBytes) &&
    TileBytes.Num() > 0;
  Statement.Destroy();
  Database.Close();

  if (!bHasTile)
  {
    return false;
  }

  FUTF8ToTCHAR Converted(
    reinterpret_cast<const ANSICHAR*>(TileBytes.GetData()), TileBytes.Num());
  OutPayload = FString(Converted.Length(), Converted.Get());
  return true;
}

bool LoadLocalTilePayload(const FFlyingNavigationMapTilePackage& TilePackage, FString& OutPayload)
{
  if (TilePackage.TileArchiveFormat.Equals(TEXT("pmtiles"), ESearchCase::IgnoreCase))
  {
    return LoadPmtilesTilePayload(TilePackage.ResolvedTileArchivePath, OutPayload);
  }
  if (TilePackage.TileArchiveFormat.Equals(TEXT("mbtiles"), ESearchCase::IgnoreCase))
  {
    return LoadMbtilesTilePayload(TilePackage.ResolvedTileArchivePath, OutPayload);
  }
  return false;
}

bool JsonPointToCanvas(const TSharedPtr<FJsonObject>& PointObject, FVector2D& OutPoint)
{
  if (!PointObject.IsValid())
  {
    return false;
  }

  double EastMeters = 0.0;
  double NorthMeters = 0.0;
  if (!PointObject->TryGetNumberField(TEXT("eastM"), EastMeters) ||
      !PointObject->TryGetNumberField(TEXT("northM"), NorthMeters))
  {
    return false;
  }

  OutPoint = TilePoint(EastMeters, NorthMeters);
  return true;
}

FLinearColor ColorForTileLayer(const FString& LayerId, const FString& Category)
{
  if (LayerId == TEXT("runways"))
  {
    return FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);
  }
  if (LayerId == TEXT("obstacles"))
  {
    return FLinearColor(1.0f, 0.35f, 0.27f, 1.0f);
  }
  if (LayerId == TEXT("airspaces"))
  {
    return FLinearColor(0.31f, 0.72f, 1.0f, 0.9f);
  }
  if (LayerId == TEXT("airports"))
  {
    return FLinearColor(0.95f, 0.97f, 0.98f, 1.0f);
  }
  if (Category == TEXT("water"))
  {
    return FLinearColor(0.12f, 0.26f, 0.33f, 1.0f);
  }
  if (Category == TEXT("road") || Category == TEXT("transport"))
  {
    return FLinearColor(0.32f, 0.40f, 0.43f, 1.0f);
  }
  return FLinearColor(0.39f, 0.47f, 0.41f, 1.0f);
}

float StrokeWidthForTileLayer(const FString& LayerId, const FString& Category)
{
  if (LayerId == TEXT("runways"))
  {
    return 3.0f;
  }
  if (LayerId == TEXT("obstacles") || LayerId == TEXT("airports"))
  {
    return 2.0f;
  }
  if (Category == TEXT("water"))
  {
    return 4.0f;
  }
  return 1.0f;
}

void DrawPointSymbol(FPaintContext& Context,
                     const FVector2D& Origin,
                     const FVector2D& Point,
                     const FLinearColor& Color,
                     float Radius)
{
  UWidgetBlueprintLibrary::DrawLine(
    Context, Origin + Point + FVector2D(-Radius, 0.0), Origin + Point + FVector2D(Radius, 0.0),
    Color, true, 2.0f);
  UWidgetBlueprintLibrary::DrawLine(
    Context, Origin + Point + FVector2D(0.0, -Radius), Origin + Point + FVector2D(0.0, Radius),
    Color, true, 2.0f);
}

void DrawFeatureGeometry(FPaintContext& Context,
                         const FVector2D& Origin,
                         const FString& LayerId,
                         const TSharedPtr<FJsonObject>& FeatureObject)
{
  if (!FeatureObject.IsValid())
  {
    return;
  }

  FString Category;
  FeatureObject->TryGetStringField(TEXT("category"), Category);
  TSharedPtr<FJsonObject> GeometryObject;
  if (!FeatureObject->TryGetObjectField(TEXT("geometry"), GeometryObject) ||
      !GeometryObject.IsValid())
  {
    return;
  }

  const TArray<TSharedPtr<FJsonValue>>* Parts = nullptr;
  if (!GeometryObject->TryGetArrayField(TEXT("parts"), Parts))
  {
    return;
  }

  const FLinearColor Color = ColorForTileLayer(LayerId, Category);
  const float StrokeWidth = StrokeWidthForTileLayer(LayerId, Category);
  for (const TSharedPtr<FJsonValue>& PartValue : *Parts)
  {
    const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
    if (!PartValue.IsValid() || !PartValue->TryGetArray(Points) || Points == nullptr)
    {
      continue;
    }
    if (Points->Num() == 1)
    {
      FVector2D Point;
      if (JsonPointToCanvas((*Points)[0]->AsObject(), Point))
      {
        DrawPointSymbol(Context, Origin, Point, Color, LayerId == TEXT("obstacles") ? 4.0f : 5.0f);
      }
      continue;
    }
    for (int32 Index = 1; Index < Points->Num(); ++Index)
    {
      FVector2D Previous;
      FVector2D Current;
      if (JsonPointToCanvas((*Points)[Index - 1]->AsObject(), Previous) &&
          JsonPointToCanvas((*Points)[Index]->AsObject(), Current))
      {
        UWidgetBlueprintLibrary::DrawLine(
          Context, Origin + Previous, Origin + Current, Color, true, StrokeWidth);
      }
    }
  }
}

bool RenderDecodedTileLayer(FPaintContext& Context,
                            const FString& PayloadJson,
                            const FString& LayerId,
                            const FVector2D& Origin)
{
  TSharedPtr<FJsonObject> Root;
  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadJson);
  if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
  {
    return false;
  }

  TSharedPtr<FJsonObject> LayersObject;
  if (!Root->TryGetObjectField(TEXT("layers"), LayersObject) ||
      !LayersObject.IsValid())
  {
    return false;
  }

  TSharedPtr<FJsonObject> LayerObject;
  if (!LayersObject->TryGetObjectField(LayerId, LayerObject) ||
      !LayerObject.IsValid())
  {
    return false;
  }

  if (LayerId == TEXT("geonames-labels"))
  {
    const TArray<TSharedPtr<FJsonValue>>* Labels = nullptr;
    if (LayerObject->TryGetArrayField(TEXT("labels"), Labels))
    {
      for (const TSharedPtr<FJsonValue>& LabelValue : *Labels)
      {
        const TSharedPtr<FJsonObject> LabelObject =
          LabelValue.IsValid() ? LabelValue->AsObject() : nullptr;
        TSharedPtr<FJsonObject> PositionObject;
        FVector2D Point;
        if (LabelObject.IsValid() &&
            LabelObject->TryGetObjectField(TEXT("position"), PositionObject) &&
            JsonPointToCanvas(PositionObject, Point))
        {
          UWidgetBlueprintLibrary::DrawLine(
            Context, Origin + Point + FVector2D(-8.0, 0.0),
            Origin + Point + FVector2D(8.0, 0.0),
            FLinearColor(0.72f, 0.76f, 0.79f, 1.0f), true, 1.0f);
        }
      }
    }
    return true;
  }

  const TArray<TSharedPtr<FJsonValue>>* Features = nullptr;
  if (!LayerObject->TryGetArrayField(TEXT("features"), Features))
  {
    return false;
  }
  for (const TSharedPtr<FJsonValue>& FeatureValue : *Features)
  {
    DrawFeatureGeometry(Context, Origin, LayerId,
                        FeatureValue.IsValid() ? FeatureValue->AsObject() : nullptr);
  }
  return true;
}
}

UFlyingOfflineNavigationMapWidget::UFlyingOfflineNavigationMapWidget(
  const FObjectInitializer& ObjectInitializer)
  : Super(ObjectInitializer)
{
  EnsureDefaultLayerStates();
}

void UFlyingOfflineNavigationMapWidget::NativeConstruct()
{
  Super::NativeConstruct();

  const UFlyingPresentationSettings* Settings =
    GetDefault<UFlyingPresentationSettings>();
  if (MapManifestPath.IsEmpty())
  {
    MapManifestPath = Settings->NavigationMapManifestPath;
  }
  if (MapStylePath.IsEmpty())
  {
    MapStylePath = Settings->NavigationMapStylePath;
  }
  EnsureDefaultLayerStates();
}

void UFlyingOfflineNavigationMapWidget::EnsureDefaultLayerStates()
{
  const EFlyingNavigationMapLayer Required[] = {
    EFlyingNavigationMapLayer::Airports,
    EFlyingNavigationMapLayer::Runways,
    EFlyingNavigationMapLayer::Obstacles,
    EFlyingNavigationMapLayer::Airspaces,
    EFlyingNavigationMapLayer::Labels,
    EFlyingNavigationMapLayer::AircraftPosition,
    EFlyingNavigationMapLayer::FlightPath,
    EFlyingNavigationMapLayer::ReplayTrack,
  };

  for (EFlyingNavigationMapLayer Layer : Required)
  {
    if (!LayerStates.ContainsByPredicate(
          [Layer](const FFlyingNavigationMapLayerState& State)
          {
            return State.Layer == Layer;
          }))
    {
      FFlyingNavigationMapLayerState State;
      State.Layer = Layer;
      State.bVisible = true;
      LayerStates.Add(State);
    }
  }
}

bool UFlyingOfflineNavigationMapWidget::InitializeOfflineMap()
{
  bInitialized = false;
  LocalTilePackages.Reset();
  AttributionText.Reset();
  EnsureDefaultLayerStates();

  if (!LoadStyle())
  {
    return false;
  }
  if (!LoadManifest())
  {
    return false;
  }

  bInitialized = true;
  LastStatus = TEXT("Offline navigation map initialized");
  return true;
}

bool UFlyingOfflineNavigationMapWidget::LoadStyle()
{
  FString StyleJson;
  const FString ResolvedStylePath = ResolveProjectPath(MapStylePath);
  if (!FFileHelper::LoadFileToString(StyleJson, *ResolvedStylePath))
  {
    LastStatus = TEXT("Offline navigation map style is missing");
    return false;
  }
  if (HasRemoteToken(StyleJson))
  {
    LastStatus = TEXT("Offline navigation map style contains a remote reference");
    return false;
  }

  TSharedPtr<FJsonObject> StyleRoot;
  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(StyleJson);
  if (!FJsonSerializer::Deserialize(Reader, StyleRoot) || !StyleRoot.IsValid())
  {
    LastStatus = TEXT("Offline navigation map style is not valid JSON");
    return false;
  }

  return true;
}

bool UFlyingOfflineNavigationMapWidget::LoadManifest()
{
  FString ManifestJson;
  const FString ResolvedManifestPath = ResolveProjectPath(MapManifestPath);
  if (!FFileHelper::LoadFileToString(ManifestJson, *ResolvedManifestPath))
  {
    LastStatus = TEXT("Offline navigation map manifest is missing");
    return false;
  }
  if (HasRemoteToken(ManifestJson))
  {
    LastStatus = TEXT("Offline navigation map manifest contains a remote reference");
    return false;
  }

  TSharedPtr<FJsonObject> Root;
  const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ManifestJson);
  if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
  {
    LastStatus = TEXT("Offline navigation map manifest is not valid JSON");
    return false;
  }

  TSharedPtr<FJsonObject> RuntimeDependencies;
  if (Root->TryGetObjectField(TEXT("runtimeDependencies"), RuntimeDependencies) &&
      RuntimeDependencies.IsValid())
  {
    bool bRuntimeNetworkRequired = true;
    RuntimeDependencies->TryGetBoolField(
      TEXT("runtimeNetworkRequired"), bRuntimeNetworkRequired);
    if (bRuntimeNetworkRequired)
    {
      LastStatus = TEXT("Offline navigation map cannot require runtime network");
      return false;
    }
  }

  const TArray<TSharedPtr<FJsonValue>>* TilePackages = nullptr;
  if (!Root->TryGetArrayField(TEXT("tilePackages"), TilePackages))
  {
    LastStatus = TEXT("Offline navigation map manifest has no tile packages");
    return false;
  }

  TSet<FString> LoadedLayers;
  for (const TSharedPtr<FJsonValue>& Value : *TilePackages)
  {
    const TSharedPtr<FJsonObject> Package = Value.IsValid() ? Value->AsObject() : nullptr;
    if (!Package.IsValid())
    {
      LastStatus = TEXT("Offline navigation map tile package entry is invalid");
      return false;
    }

    FFlyingNavigationMapTilePackage Loaded;
    Package->TryGetStringField(TEXT("layerId"), Loaded.LayerId);
    Package->TryGetStringField(TEXT("path"), Loaded.TileArchivePath);
    Package->TryGetStringField(TEXT("format"), Loaded.TileArchiveFormat);
    Package->TryGetStringField(TEXT("attribution"), Loaded.AttributionText);

    if (!IsLocalTileArchivePath(Loaded.TileArchivePath))
    {
      LastStatus = TEXT("Offline navigation map tile package is not a local MBTiles or PMTiles archive");
      return false;
    }
    if (!DoesFormatMatchArchivePath(Loaded.TileArchiveFormat, Loaded.TileArchivePath))
    {
      LastStatus = FString::Printf(
        TEXT("Offline navigation map tile package '%s' format does not match archive extension"),
        *Loaded.LayerId);
      return false;
    }

    const FString ManifestDirectory = FPaths::GetPath(ResolvedManifestPath);
    Loaded.ResolvedTileArchivePath = Loaded.TileArchivePath;
    if (FPaths::IsRelative(Loaded.ResolvedTileArchivePath))
    {
      Loaded.ResolvedTileArchivePath = FPaths::ConvertRelativePathToFull(
        FPaths::ProjectDir(), Loaded.ResolvedTileArchivePath);
      if (!FPaths::FileExists(Loaded.ResolvedTileArchivePath))
      {
        Loaded.ResolvedTileArchivePath = FPaths::ConvertRelativePathToFull(
          ManifestDirectory, FPaths::GetCleanFilename(Loaded.TileArchivePath));
      }
    }
    FPaths::NormalizeFilename(Loaded.ResolvedTileArchivePath);
    FPaths::CollapseRelativeDirectories(Loaded.ResolvedTileArchivePath);
    if (!FPaths::FileExists(Loaded.ResolvedTileArchivePath))
    {
      LastStatus = FString::Printf(
        TEXT("Offline navigation map tile package '%s' archive is missing"),
        *Loaded.LayerId);
      return false;
    }
    if (!HasExpectedArchiveMagic(Loaded.TileArchiveFormat, Loaded.ResolvedTileArchivePath))
    {
      LastStatus = FString::Printf(
        TEXT("Offline navigation map tile package '%s' is not a valid local %s archive"),
        *Loaded.LayerId,
        *Loaded.TileArchiveFormat);
      return false;
    }

    LoadedLayers.Add(Loaded.LayerId);
    LocalTilePackages.Add(Loaded);

    if (!Loaded.AttributionText.IsEmpty())
    {
      if (!AttributionText.IsEmpty())
      {
        AttributionText += TEXT(" | ");
      }
      AttributionText += Loaded.AttributionText;
    }
  }

  for (const auto& RequiredLayer : kRequiredLayers)
  {
    if (!LoadedLayers.Contains(RequiredLayer))
    {
      LastStatus = FString::Printf(
        TEXT("Offline navigation map missing required layer '%s'"), RequiredLayer);
      return false;
    }
  }

  if (AttributionText.IsEmpty())
  {
    LastStatus = TEXT("Offline navigation map attribution is missing");
    return false;
  }

  return true;
}

void UFlyingOfflineNavigationMapWidget::SetLayerVisible(
  EFlyingNavigationMapLayer Layer,
  bool bVisible)
{
  EnsureDefaultLayerStates();
  for (FFlyingNavigationMapLayerState& State : LayerStates)
  {
    if (State.Layer == Layer)
    {
      State.bVisible = bVisible;
      return;
    }
  }
}

bool UFlyingOfflineNavigationMapWidget::IsLayerVisible(
  EFlyingNavigationMapLayer Layer) const
{
  for (const FFlyingNavigationMapLayerState& State : LayerStates)
  {
    if (State.Layer == Layer)
    {
      return State.bVisible;
    }
  }
  return true;
}

void UFlyingOfflineNavigationMapWidget::SetAircraftPositionLocal(
  const FFlyingNavigationMapOverlayPoint& Position)
{
  AircraftPosition = Position;
}

void UFlyingOfflineNavigationMapWidget::AppendFlightPathPoint(
  const FFlyingNavigationMapOverlayPoint& Position)
{
  FlightPath.Add(Position);
}

void UFlyingOfflineNavigationMapWidget::SetFlightPath(
  const TArray<FFlyingNavigationMapOverlayPoint>& Points)
{
  FlightPath = Points;
}

void UFlyingOfflineNavigationMapWidget::SetReplayTrack(
  const TArray<FFlyingNavigationMapOverlayPoint>& Points)
{
  ReplayTrack = Points;
}

void UFlyingOfflineNavigationMapWidget::ClearFlightPath()
{
  FlightPath.Reset();
}

void UFlyingOfflineNavigationMapWidget::RenderMapToPaintContext(
  FPaintContext& Context) const
{
  if (!bInitialized)
  {
    return;
  }

  const FVector2D Origin(320.0, 240.0);
  for (const FFlyingNavigationMapTilePackage& TilePackage : LocalTilePackages)
  {
    RenderTilePackageLayer(Context, TilePackage, Origin);
  }

  if (IsLayerVisible(EFlyingNavigationMapLayer::FlightPath) && FlightPath.Num() > 1)
  {
    for (int32 Index = 1; Index < FlightPath.Num(); ++Index)
    {
      UWidgetBlueprintLibrary::DrawLine(
        Context,
        Origin + ToCanvasPoint(FlightPath[Index - 1]),
        Origin + ToCanvasPoint(FlightPath[Index]),
        FLinearColor(0.05f, 0.55f, 0.95f, 1.0f),
        true,
        2.0f);
    }
  }

  if (IsLayerVisible(EFlyingNavigationMapLayer::ReplayTrack) && ReplayTrack.Num() > 1)
  {
    for (int32 Index = 1; Index < ReplayTrack.Num(); ++Index)
    {
      UWidgetBlueprintLibrary::DrawLine(
        Context,
        Origin + ToCanvasPoint(ReplayTrack[Index - 1]),
        Origin + ToCanvasPoint(ReplayTrack[Index]),
        FLinearColor(0.95f, 0.65f, 0.15f, 1.0f),
        true,
        2.0f);
    }
  }

  if (IsLayerVisible(EFlyingNavigationMapLayer::AircraftPosition))
  {
    const FVector2D AircraftCanvas = Origin + ToCanvasPoint(AircraftPosition);
    UWidgetBlueprintLibrary::DrawLine(
      Context,
      AircraftCanvas + FVector2D(-8.0, 0.0),
      AircraftCanvas + FVector2D(8.0, 0.0),
      FLinearColor::White,
      true,
      2.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context,
      AircraftCanvas + FVector2D(0.0, -8.0),
      AircraftCanvas + FVector2D(0.0, 8.0),
      FLinearColor::White,
      true,
      2.0f);
  }
}

void UFlyingOfflineNavigationMapWidget::RenderTilePackageLayer(
  FPaintContext& Context,
  const FFlyingNavigationMapTilePackage& TilePackage,
  const FVector2D& Origin) const
{
  if (TilePackage.LayerId == TEXT("zabaged-base"))
  {
    FString PayloadJson;
    if (LoadLocalTilePayload(TilePackage, PayloadJson) &&
        RenderDecodedTileLayer(Context, PayloadJson, TilePackage.LayerId, Origin))
    {
      return;
    }

    const FLinearColor RoadColor(0.32f, 0.40f, 0.43f, 1.0f);
    const FLinearColor WaterColor(0.12f, 0.26f, 0.33f, 1.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(-9000.0, -5000.0), Origin + TilePoint(9000.0, 4200.0),
      RoadColor, true, 1.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(-7200.0, 3400.0), Origin + TilePoint(8600.0, 3600.0),
      WaterColor, true, 4.0f);
    return;
  }

  const EFlyingNavigationMapLayer Layer = LayerForPackageId(TilePackage.LayerId);
  if (!IsLayerVisible(Layer))
  {
    return;
  }

  FString PayloadJson;
  if (LoadLocalTilePayload(TilePackage, PayloadJson) &&
      RenderDecodedTileLayer(Context, PayloadJson, TilePackage.LayerId, Origin))
  {
    return;
  }

  if (TilePackage.LayerId == TEXT("geonames-labels"))
  {
    const FLinearColor LabelColor(0.72f, 0.76f, 0.79f, 1.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(-5200.0, 1800.0), Origin + TilePoint(-4700.0, 1800.0),
      LabelColor, true, 1.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(2500.0, -1600.0), Origin + TilePoint(3050.0, -1600.0),
      LabelColor, true, 1.0f);
    return;
  }

  if (TilePackage.LayerId == TEXT("airports"))
  {
    const FLinearColor AirportColor(0.95f, 0.97f, 0.98f, 1.0f);
    const FVector2D Points[] = {
      TilePoint(-2500.0, 1200.0),
      TilePoint(4200.0, -2600.0),
    };
    for (const FVector2D& Point : Points)
    {
      UWidgetBlueprintLibrary::DrawLine(
        Context, Origin + Point + FVector2D(-5.0, 0.0), Origin + Point + FVector2D(5.0, 0.0),
        AirportColor, true, 2.0f);
      UWidgetBlueprintLibrary::DrawLine(
        Context, Origin + Point + FVector2D(0.0, -5.0), Origin + Point + FVector2D(0.0, 5.0),
        AirportColor, true, 2.0f);
    }
    return;
  }

  if (TilePackage.LayerId == TEXT("runways"))
  {
    const FLinearColor RunwayColor(1.0f, 1.0f, 1.0f, 1.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(-3100.0, 1050.0), Origin + TilePoint(-1900.0, 1350.0),
      RunwayColor, true, 3.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(3600.0, -3000.0), Origin + TilePoint(4800.0, -2200.0),
      RunwayColor, true, 3.0f);
    return;
  }

  if (TilePackage.LayerId == TEXT("obstacles"))
  {
    const FLinearColor ObstacleColor(1.0f, 0.35f, 0.27f, 1.0f);
    const FVector2D Points[] = {
      TilePoint(-1000.0, -800.0),
      TilePoint(5600.0, 900.0),
    };
    for (const FVector2D& Point : Points)
    {
      UWidgetBlueprintLibrary::DrawLine(
        Context, Origin + Point + FVector2D(-4.0, 4.0), Origin + Point + FVector2D(4.0, -4.0),
        ObstacleColor, true, 2.0f);
      UWidgetBlueprintLibrary::DrawLine(
        Context, Origin + Point + FVector2D(-4.0, -4.0), Origin + Point + FVector2D(4.0, 4.0),
        ObstacleColor, true, 2.0f);
    }
    return;
  }

  if (TilePackage.LayerId == TEXT("airspaces"))
  {
    const FLinearColor AirspaceColor(0.31f, 0.72f, 1.0f, 0.9f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(-6200.0, -2600.0), Origin + TilePoint(-2000.0, 4100.0),
      AirspaceColor, true, 1.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(-2000.0, 4100.0), Origin + TilePoint(5200.0, 2800.0),
      AirspaceColor, true, 1.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(5200.0, 2800.0), Origin + TilePoint(6100.0, -3100.0),
      AirspaceColor, true, 1.0f);
    UWidgetBlueprintLibrary::DrawLine(
      Context, Origin + TilePoint(6100.0, -3100.0), Origin + TilePoint(-6200.0, -2600.0),
      AirspaceColor, true, 1.0f);
  }
}
