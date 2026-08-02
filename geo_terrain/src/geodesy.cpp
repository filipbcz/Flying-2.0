#include "flying/geo_terrain/geodesy.hpp"

#include <cmath>

namespace flying::geo_terrain {
namespace {

[[nodiscard]] constexpr Vector3d ned_as_vector(NedVector ned) noexcept {
  return {ned.north_m, ned.east_m, ned.down_m};
}

[[nodiscard]] constexpr NedVector vector_as_ned(Vector3d vector) noexcept {
  return {vector.x, vector.y, vector.z};
}

[[nodiscard]] constexpr Vector3d body_as_vector(BodyVector body) noexcept {
  return {body.forward, body.right, body.down};
}

[[nodiscard]] constexpr BodyVector vector_as_body(Vector3d vector) noexcept {
  return {vector.x, vector.y, vector.z};
}

} // namespace

EcefPosition geodetic_to_ecef(GeodeticCoordinates geodetic) noexcept {
  const double sin_lat = std::sin(geodetic.latitude_rad);
  const double cos_lat = std::cos(geodetic.latitude_rad);
  const double sin_lon = std::sin(geodetic.longitude_rad);
  const double cos_lon = std::cos(geodetic.longitude_rad);
  const double prime_vertical_radius =
      Wgs84Ellipsoid::semi_major_axis_m /
      std::sqrt(1.0 - Wgs84Ellipsoid::first_eccentricity_squared * sin_lat * sin_lat);
  const double height_m = geodetic.ellipsoidal_height.meters;

  return {{
    (prime_vertical_radius + height_m) * cos_lat * cos_lon,
    (prime_vertical_radius + height_m) * cos_lat * sin_lon,
    (prime_vertical_radius * (1.0 - Wgs84Ellipsoid::first_eccentricity_squared) + height_m) * sin_lat,
  }};
}

GeodeticCoordinates ecef_to_geodetic(EcefPosition ecef) noexcept {
  const double x = ecef.meters.x;
  const double y = ecef.meters.y;
  const double z = ecef.meters.z;
  const double horizontal_radius = std::hypot(x, y);
  const double longitude_rad = std::atan2(y, x);

  if (horizontal_radius < 1.0e-12) {
    const double latitude_rad = z < 0.0 ? -kPi / 2.0 : kPi / 2.0;
    return {
      latitude_rad,
      longitude_rad,
      {std::abs(z) - Wgs84Ellipsoid::semi_minor_axis_m},
    };
  }

  const double theta = std::atan2(z * Wgs84Ellipsoid::semi_major_axis_m,
                                  horizontal_radius * Wgs84Ellipsoid::semi_minor_axis_m);
  const double sin_theta = std::sin(theta);
  const double cos_theta = std::cos(theta);
  const double latitude_rad = std::atan2(
      z + Wgs84Ellipsoid::second_eccentricity_squared * Wgs84Ellipsoid::semi_minor_axis_m *
              sin_theta * sin_theta * sin_theta,
      horizontal_radius - Wgs84Ellipsoid::first_eccentricity_squared * Wgs84Ellipsoid::semi_major_axis_m *
              cos_theta * cos_theta * cos_theta);

  const double sin_lat = std::sin(latitude_rad);
  const double cos_lat = std::cos(latitude_rad);
  const double prime_vertical_radius =
      Wgs84Ellipsoid::semi_major_axis_m /
      std::sqrt(1.0 - Wgs84Ellipsoid::first_eccentricity_squared * sin_lat * sin_lat);
  const double height_m = std::abs(cos_lat) > 1.0e-12
                              ? horizontal_radius / cos_lat - prime_vertical_radius
                              : z / sin_lat -
                                    prime_vertical_radius * (1.0 - Wgs84Ellipsoid::first_eccentricity_squared);

  return {latitude_rad, longitude_rad, {height_m}};
}

LocalTangentFrame make_local_tangent_frame(GeodeticCoordinates origin) noexcept {
  const double sin_lat = std::sin(origin.latitude_rad);
  const double cos_lat = std::cos(origin.latitude_rad);
  const double sin_lon = std::sin(origin.longitude_rad);
  const double cos_lon = std::cos(origin.longitude_rad);

  return {
    origin,
    geodetic_to_ecef(origin),
    {-sin_lon, cos_lon, 0.0},
    {-sin_lat * cos_lon, -sin_lat * sin_lon, cos_lat},
    {cos_lat * cos_lon, cos_lat * sin_lon, sin_lat},
  };
}

EcefVector ecef_vector_from_enu(LocalTangentFrame frame, EnuVector enu) noexcept {
  return {
    frame.east_unit_ecef * enu.east_m +
    frame.north_unit_ecef * enu.north_m +
    frame.up_unit_ecef * enu.up_m,
  };
}

EcefVector ecef_vector_from_ned(LocalTangentFrame frame, NedVector ned) noexcept {
  return {
    frame.north_unit_ecef * ned.north_m +
    frame.east_unit_ecef * ned.east_m -
    frame.up_unit_ecef * ned.down_m,
  };
}

EnuVector enu_from_ecef_vector(LocalTangentFrame frame, EcefVector ecef) noexcept {
  return {
    dot(ecef.meters, frame.east_unit_ecef),
    dot(ecef.meters, frame.north_unit_ecef),
    dot(ecef.meters, frame.up_unit_ecef),
  };
}

NedVector ned_from_ecef_vector(LocalTangentFrame frame, EcefVector ecef) noexcept {
  return {
    dot(ecef.meters, frame.north_unit_ecef),
    dot(ecef.meters, frame.east_unit_ecef),
    -dot(ecef.meters, frame.up_unit_ecef),
  };
}

EcefPosition ecef_from_enu(LocalTangentFrame frame, EnuVector enu) noexcept {
  return frame.origin_ecef + ecef_vector_from_enu(frame, enu);
}

EcefPosition ecef_from_ned(LocalTangentFrame frame, NedVector ned) noexcept {
  return frame.origin_ecef + ecef_vector_from_ned(frame, ned);
}

EnuVector enu_from_ecef_position(LocalTangentFrame frame, EcefPosition ecef) noexcept {
  return enu_from_ecef_vector(frame, ecef - frame.origin_ecef);
}

NedVector ned_from_ecef_position(LocalTangentFrame frame, EcefPosition ecef) noexcept {
  return ned_from_ecef_vector(frame, ecef - frame.origin_ecef);
}

AnchoredLocalPosition anchor_in_frame(LocalTangentFrame frame, EcefPosition authoritative_ecef) noexcept {
  return {authoritative_ecef, enu_from_ecef_position(frame, authoritative_ecef)};
}

AnchoredLocalPosition reanchor_in_frame(
    LocalTangentFrame new_frame,
    AnchoredLocalPosition position) noexcept {
  return anchor_in_frame(new_frame, position.authoritative_ecef);
}

Matrix3d body_to_ned_matrix(BodyEulerAngles body_to_ned) noexcept {
  const double sin_roll = std::sin(body_to_ned.roll_rad);
  const double cos_roll = std::cos(body_to_ned.roll_rad);
  const double sin_pitch = std::sin(body_to_ned.pitch_rad);
  const double cos_pitch = std::cos(body_to_ned.pitch_rad);
  const double sin_yaw = std::sin(body_to_ned.yaw_rad);
  const double cos_yaw = std::cos(body_to_ned.yaw_rad);

  return {
    cos_pitch * cos_yaw,
    sin_roll * sin_pitch * cos_yaw - cos_roll * sin_yaw,
    cos_roll * sin_pitch * cos_yaw + sin_roll * sin_yaw,
    cos_pitch * sin_yaw,
    sin_roll * sin_pitch * sin_yaw + cos_roll * cos_yaw,
    cos_roll * sin_pitch * sin_yaw - sin_roll * cos_yaw,
    -sin_pitch,
    sin_roll * cos_pitch,
    cos_roll * cos_pitch,
  };
}

Matrix3d ned_to_body_matrix(BodyEulerAngles body_to_ned) noexcept {
  return body_to_ned_matrix(body_to_ned).transposed();
}

NedVector ned_from_body_vector(BodyEulerAngles body_to_ned, BodyVector body) noexcept {
  return vector_as_ned(body_to_ned_matrix(body_to_ned) * body_as_vector(body));
}

BodyVector body_from_ned_vector(BodyEulerAngles body_to_ned, NedVector ned) noexcept {
  return vector_as_body(ned_to_body_matrix(body_to_ned) * ned_as_vector(ned));
}

EcefVector ecef_vector_from_body(
    LocalTangentFrame frame,
    BodyEulerAngles body_to_ned,
    BodyVector body) noexcept {
  return ecef_vector_from_ned(frame, ned_from_body_vector(body_to_ned, body));
}

BodyVector body_vector_from_ecef(
    LocalTangentFrame frame,
    BodyEulerAngles body_to_ned,
    EcefVector ecef) noexcept {
  return body_from_ned_vector(body_to_ned, ned_from_ecef_vector(frame, ecef));
}

} // namespace flying::geo_terrain
