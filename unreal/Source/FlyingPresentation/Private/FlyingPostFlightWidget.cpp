#include "FlyingPostFlightWidget.h"

#include "FlyingCoreSimComponent.h"
#include "flying/geo_terrain/geodesy.hpp"

namespace
{
TArray<FFlyingNavigationMapOverlayPoint> MakeMapTrack(
  const TArray<FFlyingTelemetryRoutePoint>& Route)
{
  TArray<FFlyingNavigationMapOverlayPoint> Track;
  if (Route.Num() == 0)
  {
    return Track;
  }

  const flying::geo_terrain::GeodeticCoordinates OriginGeodetic =
    flying::geo_terrain::make_geodetic_degrees(
      Route[0].LatitudeDegrees,
      Route[0].LongitudeDegrees,
      flying::geo_terrain::EllipsoidalHeight{Route[0].AltitudeMeters});
  const flying::geo_terrain::LocalTangentFrame Frame =
    flying::geo_terrain::make_local_tangent_frame(OriginGeodetic);

  Track.Reserve(Route.Num());
  for (const FFlyingTelemetryRoutePoint& RoutePoint : Route)
  {
    const flying::geo_terrain::GeodeticCoordinates PointGeodetic =
      flying::geo_terrain::make_geodetic_degrees(
        RoutePoint.LatitudeDegrees,
        RoutePoint.LongitudeDegrees,
        flying::geo_terrain::EllipsoidalHeight{RoutePoint.AltitudeMeters});
    const flying::geo_terrain::EcefPosition Ecef =
      flying::geo_terrain::geodetic_to_ecef(PointGeodetic);
    const flying::geo_terrain::EnuVector Enu =
      flying::geo_terrain::enu_from_ecef_position(Frame, Ecef);

    FFlyingNavigationMapOverlayPoint MapPoint;
    MapPoint.EastMeters = Enu.east_m;
    MapPoint.NorthMeters = Enu.north_m;
    MapPoint.AltitudeMeters = RoutePoint.AltitudeMeters;
    MapPoint.TimeSeconds = RoutePoint.TimeSeconds;
    Track.Add(MapPoint);
  }
  return Track;
}
}

bool UFlyingPostFlightWidget::LoadReplay(
  UFlyingCoreSimComponent* CoreSimComponent,
  bool bWarnOnIncompatible)
{
  const bool bLoaded =
    CoreSimComponent && CoreSimComponent->LoadTelemetryReplay(ReplayPath, bWarnOnIncompatible);
  LastStatus = CoreSimComponent ? CoreSimComponent->LastTelemetryStatus : TEXT("CoreSim component is missing");
  if (bLoaded)
  {
    RefreshPostFlightData(CoreSimComponent);
  }
  return bLoaded;
}

bool UFlyingPostFlightWidget::PlayReplay(
  UFlyingCoreSimComponent* CoreSimComponent,
  bool bWarnOnIncompatible)
{
  const bool bPlayed =
    CoreSimComponent && CoreSimComponent->PlayLoadedTelemetryReplay(bWarnOnIncompatible);
  LastStatus = CoreSimComponent ? CoreSimComponent->LastTelemetryStatus : TEXT("CoreSim component is missing");
  return bPlayed;
}

bool UFlyingPostFlightWidget::RefreshPostFlightData(UFlyingCoreSimComponent* CoreSimComponent)
{
  if (!CoreSimComponent)
  {
    LastStatus = TEXT("CoreSim component is missing");
    Route.Reset();
    RouteMapTrack.Reset();
    Graphs.Reset();
    return false;
  }

  Route = CoreSimComponent->GetTelemetryRoutePoints();
  RouteMapTrack = MakeMapTrack(Route);
  Graphs = CoreSimComponent->GetTelemetryGraphSeries();
  LastStatus = Route.Num() > 0
                 ? TEXT("Post-flight route map and telemetry graphs refreshed")
                 : TEXT("No telemetry frames are available for post-flight review");
  return Route.Num() > 0;
}

bool UFlyingPostFlightWidget::ExportCsv(UFlyingCoreSimComponent* CoreSimComponent)
{
  const bool bExported =
    CoreSimComponent && CoreSimComponent->ExportTelemetryCsv(CsvExportPath);
  LastStatus = CoreSimComponent ? CoreSimComponent->LastTelemetryStatus : TEXT("CoreSim component is missing");
  return bExported;
}

bool UFlyingPostFlightWidget::ExportJson(UFlyingCoreSimComponent* CoreSimComponent)
{
  const bool bExported =
    CoreSimComponent && CoreSimComponent->ExportTelemetryJson(JsonExportPath);
  LastStatus = CoreSimComponent ? CoreSimComponent->LastTelemetryStatus : TEXT("CoreSim component is missing");
  return bExported;
}
