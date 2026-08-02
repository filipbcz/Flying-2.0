# Geo/Terrain

Native C++ boundary for shared geodesy, height semantics, unit conversion and future terrain height,
surface metadata and collision authority.

## Current Scope

This module now owns the atmosphere-independent geodesy primitives required by CoreSim, terrain
tools and Unreal integration:

- WGS-84 ellipsoid constants using double precision.
- Geodetic latitude/longitude plus **ellipsoidal height** to ECEF conversion, and ECEF back to
  geodetic coordinates.
- Local tangent frames with ENU and NED vector/position conversion. `AnchoredLocalPosition` keeps
  the authoritative ECEF position while recalculating local offsets for render-origin shifts.
- Aerospace body-frame vector transforms using roll, pitch and yaw relative to NED.
- Value objects for ellipsoidal height, orthometric height, geoid undulation, pressure altitude,
  QNH, QFE and indicated instrument altitude.
- Unit boundary conversions for degrees/radians, meters/feet, meters/nautical miles,
  meters-per-second/knots and Pa/hPa/inHg.

## Conventions

- Internal simulation and geodesy quantities are SI unless a type name states otherwise.
- `GeodeticCoordinates` stores radians and requires `EllipsoidalHeight`; orthometric terrain heights
  must be converted explicitly with a geoid undulation before ECEF geometry is produced.
- ECEF is authoritative and always represented in meters as `double`.
- ENU and NED offsets are local tangent-plane values relative to a `LocalTangentFrame`. They are not
  authoritative global positions.
- Body axes follow the aerospace convention: `forward`, `right`, `down`. With zero roll, pitch and
  yaw, body forward aligns with NED north, body right with east and body down with down.

## Numerical Tolerances

Reference tests enforce these practical tolerances for supported inputs near normal flight and
terrain altitudes:

- WGS-84 axis reference ECEF values: `1e-6 m`.
- Non-trivial geodetic to ECEF reference values: `1e-6 m`.
- ECEF back to geodetic round trips: `1e-10 deg` latitude/longitude and `1e-6 m` height.
- Local ENU/NED and body-frame vector reference transforms: `1e-9 m` or unit-vector equivalent.
- Re-anchoring across local origin shifts copies authoritative ECEF coordinates exactly and
  recomputes only the derived local offset.

This scope intentionally excludes PROJ-backed GIS source-data transformations, DEM processing,
runway surface logic, Cesium rendering integration and terrain collision implementation.
