#pragma once

#include "flying/geo_terrain/heights.hpp"
#include "flying/geo_terrain/math.hpp"
#include "flying/geo_terrain/units.hpp"

namespace flying::geo_terrain {

struct Wgs84Ellipsoid {
  static constexpr double semi_major_axis_m = 6'378'137.0;
  static constexpr double inverse_flattening = 298.257223563;
  static constexpr double flattening = 1.0 / inverse_flattening;
  static constexpr double semi_minor_axis_m = semi_major_axis_m * (1.0 - flattening);
  static constexpr double first_eccentricity_squared = flattening * (2.0 - flattening);
  static constexpr double second_eccentricity_squared =
      (semi_major_axis_m * semi_major_axis_m - semi_minor_axis_m * semi_minor_axis_m) /
      (semi_minor_axis_m * semi_minor_axis_m);
};

struct GeodeticCoordinates {
  double latitude_rad{};
  double longitude_rad{};
  EllipsoidalHeight ellipsoidal_height{};

  [[nodiscard]] constexpr double latitude_degrees() const noexcept {
    return units::radians_to_degrees(latitude_rad);
  }

  [[nodiscard]] constexpr double longitude_degrees() const noexcept {
    return units::radians_to_degrees(longitude_rad);
  }
};

struct EcefVector {
  Vector3d meters{};
};

struct EcefPosition {
  Vector3d meters{};
};

struct EnuVector {
  double east_m{};
  double north_m{};
  double up_m{};
};

struct NedVector {
  double north_m{};
  double east_m{};
  double down_m{};
};

struct BodyVector {
  double forward{};
  double right{};
  double down{};
};

struct BodyEulerAngles {
  double roll_rad{};
  double pitch_rad{};
  double yaw_rad{};
};

struct LocalTangentFrame {
  GeodeticCoordinates origin_geodetic{};
  EcefPosition origin_ecef{};
  Vector3d east_unit_ecef{};
  Vector3d north_unit_ecef{};
  Vector3d up_unit_ecef{};
};

struct AnchoredLocalPosition {
  EcefPosition authoritative_ecef{};
  EnuVector local_enu{};
};

[[nodiscard]] constexpr GeodeticCoordinates make_geodetic_radians(
    double latitude_rad,
    double longitude_rad,
    EllipsoidalHeight ellipsoidal_height) noexcept {
  return {latitude_rad, longitude_rad, ellipsoidal_height};
}

[[nodiscard]] constexpr GeodeticCoordinates make_geodetic_degrees(
    double latitude_deg,
    double longitude_deg,
    EllipsoidalHeight ellipsoidal_height) noexcept {
  return {
    units::degrees_to_radians(latitude_deg),
    units::degrees_to_radians(longitude_deg),
    ellipsoidal_height,
  };
}

[[nodiscard]] constexpr BodyEulerAngles make_body_euler_radians(
    double roll_rad,
    double pitch_rad,
    double yaw_rad) noexcept {
  return {roll_rad, pitch_rad, yaw_rad};
}

[[nodiscard]] constexpr BodyEulerAngles make_body_euler_degrees(
    double roll_deg,
    double pitch_deg,
    double yaw_deg) noexcept {
  return {
    units::degrees_to_radians(roll_deg),
    units::degrees_to_radians(pitch_deg),
    units::degrees_to_radians(yaw_deg),
  };
}

[[nodiscard]] constexpr EcefVector operator-(EcefPosition lhs, EcefPosition rhs) noexcept {
  return {lhs.meters - rhs.meters};
}

[[nodiscard]] constexpr EcefPosition operator+(EcefPosition position, EcefVector delta) noexcept {
  return {position.meters + delta.meters};
}

[[nodiscard]] constexpr EcefPosition operator-(EcefPosition position, EcefVector delta) noexcept {
  return {position.meters - delta.meters};
}

[[nodiscard]] EcefPosition geodetic_to_ecef(GeodeticCoordinates geodetic) noexcept;
[[nodiscard]] GeodeticCoordinates ecef_to_geodetic(EcefPosition ecef) noexcept;

[[nodiscard]] LocalTangentFrame make_local_tangent_frame(GeodeticCoordinates origin) noexcept;

[[nodiscard]] EcefVector ecef_vector_from_enu(LocalTangentFrame frame, EnuVector enu) noexcept;
[[nodiscard]] EcefVector ecef_vector_from_ned(LocalTangentFrame frame, NedVector ned) noexcept;
[[nodiscard]] EnuVector enu_from_ecef_vector(LocalTangentFrame frame, EcefVector ecef) noexcept;
[[nodiscard]] NedVector ned_from_ecef_vector(LocalTangentFrame frame, EcefVector ecef) noexcept;

[[nodiscard]] EcefPosition ecef_from_enu(LocalTangentFrame frame, EnuVector enu) noexcept;
[[nodiscard]] EcefPosition ecef_from_ned(LocalTangentFrame frame, NedVector ned) noexcept;
[[nodiscard]] EnuVector enu_from_ecef_position(LocalTangentFrame frame, EcefPosition ecef) noexcept;
[[nodiscard]] NedVector ned_from_ecef_position(LocalTangentFrame frame, EcefPosition ecef) noexcept;

[[nodiscard]] AnchoredLocalPosition anchor_in_frame(
    LocalTangentFrame frame,
    EcefPosition authoritative_ecef) noexcept;
[[nodiscard]] AnchoredLocalPosition reanchor_in_frame(
    LocalTangentFrame new_frame,
    AnchoredLocalPosition position) noexcept;

[[nodiscard]] Matrix3d body_to_ned_matrix(BodyEulerAngles body_to_ned) noexcept;
[[nodiscard]] Matrix3d ned_to_body_matrix(BodyEulerAngles body_to_ned) noexcept;
[[nodiscard]] NedVector ned_from_body_vector(BodyEulerAngles body_to_ned, BodyVector body) noexcept;
[[nodiscard]] BodyVector body_from_ned_vector(BodyEulerAngles body_to_ned, NedVector ned) noexcept;
[[nodiscard]] EcefVector ecef_vector_from_body(
    LocalTangentFrame frame,
    BodyEulerAngles body_to_ned,
    BodyVector body) noexcept;
[[nodiscard]] BodyVector body_vector_from_ecef(
    LocalTangentFrame frame,
    BodyEulerAngles body_to_ned,
    EcefVector ecef) noexcept;

} // namespace flying::geo_terrain
