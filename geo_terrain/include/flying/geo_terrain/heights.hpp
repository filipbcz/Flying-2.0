#pragma once

#include "flying/geo_terrain/units.hpp"

namespace flying::geo_terrain {

inline constexpr double kStandardSeaLevelPressurePa = 101'325.0;

struct EllipsoidalHeight {
  double meters{};
};

struct OrthometricHeight {
  double meters{};
};

struct GeoidUndulation {
  double meters{};
};

struct PressureAltitude {
  double meters{};
};

struct QnhSetting {
  double pressure_pa{};
};

struct QfeSetting {
  double pressure_pa{};
};

enum class IndicatedAltitudeDatum {
  kStandardPressure,
  kQnh,
  kQfe,
};

struct IndicatedAltitude {
  double meters{};
  IndicatedAltitudeDatum datum{IndicatedAltitudeDatum::kStandardPressure};
};

[[nodiscard]] constexpr EllipsoidalHeight ellipsoidal_from_orthometric(
    OrthometricHeight orthometric_height,
    GeoidUndulation geoid_undulation) noexcept {
  return {orthometric_height.meters + geoid_undulation.meters};
}

[[nodiscard]] constexpr OrthometricHeight orthometric_from_ellipsoidal(
    EllipsoidalHeight ellipsoidal_height,
    GeoidUndulation geoid_undulation) noexcept {
  return {ellipsoidal_height.meters - geoid_undulation.meters};
}

[[nodiscard]] constexpr QnhSetting qnh_from_pascals(double pressure_pa) noexcept {
  return {pressure_pa};
}

[[nodiscard]] constexpr QnhSetting qnh_from_hectopascals(double pressure_hpa) noexcept {
  return {units::pascals_from_hectopascals(units::Hectopascals{pressure_hpa}).value};
}

[[nodiscard]] constexpr QfeSetting qfe_from_pascals(double pressure_pa) noexcept {
  return {pressure_pa};
}

[[nodiscard]] constexpr QfeSetting qfe_from_hectopascals(double pressure_hpa) noexcept {
  return {units::pascals_from_hectopascals(units::Hectopascals{pressure_hpa}).value};
}

[[nodiscard]] constexpr IndicatedAltitude indicated_altitude_qnh(double meters) noexcept {
  return {meters, IndicatedAltitudeDatum::kQnh};
}

[[nodiscard]] constexpr IndicatedAltitude indicated_altitude_qfe(double meters) noexcept {
  return {meters, IndicatedAltitudeDatum::kQfe};
}

[[nodiscard]] constexpr IndicatedAltitude indicated_altitude_standard_pressure(double meters) noexcept {
  return {meters, IndicatedAltitudeDatum::kStandardPressure};
}

} // namespace flying::geo_terrain
