#pragma once

#include "flying/geo_terrain/math.hpp"

namespace flying::geo_terrain::units {

inline constexpr double kMetersPerFoot = 0.3048;
inline constexpr double kMetersPerNauticalMile = 1852.0;
inline constexpr double kSecondsPerHour = 3600.0;
inline constexpr double kMetersPerSecondPerKnot = kMetersPerNauticalMile / kSecondsPerHour;
inline constexpr double kPascalsPerHectopascal = 100.0;
inline constexpr double kPascalsPerInchMercury = 3386.389;

struct Meters {
  double value{};
};

struct Feet {
  double value{};
};

struct NauticalMiles {
  double value{};
};

struct MetersPerSecond {
  double value{};
};

struct Knots {
  double value{};
};

struct Pascals {
  double value{};
};

struct Hectopascals {
  double value{};
};

struct InchesMercury {
  double value{};
};

struct Radians {
  double value{};
};

struct Degrees {
  double value{};
};

[[nodiscard]] constexpr Meters meters_from_feet(Feet feet) noexcept {
  return {feet.value * kMetersPerFoot};
}

[[nodiscard]] constexpr Feet feet_from_meters(Meters meters) noexcept {
  return {meters.value / kMetersPerFoot};
}

[[nodiscard]] constexpr Meters meters_from_nautical_miles(NauticalMiles nautical_miles) noexcept {
  return {nautical_miles.value * kMetersPerNauticalMile};
}

[[nodiscard]] constexpr NauticalMiles nautical_miles_from_meters(Meters meters) noexcept {
  return {meters.value / kMetersPerNauticalMile};
}

[[nodiscard]] constexpr MetersPerSecond meters_per_second_from_knots(Knots knots) noexcept {
  return {knots.value * kMetersPerSecondPerKnot};
}

[[nodiscard]] constexpr Knots knots_from_meters_per_second(MetersPerSecond meters_per_second) noexcept {
  return {meters_per_second.value / kMetersPerSecondPerKnot};
}

[[nodiscard]] constexpr Pascals pascals_from_hectopascals(Hectopascals hectopascals) noexcept {
  return {hectopascals.value * kPascalsPerHectopascal};
}

[[nodiscard]] constexpr Hectopascals hectopascals_from_pascals(Pascals pascals) noexcept {
  return {pascals.value / kPascalsPerHectopascal};
}

[[nodiscard]] constexpr Pascals pascals_from_inches_mercury(InchesMercury inches_mercury) noexcept {
  return {inches_mercury.value * kPascalsPerInchMercury};
}

[[nodiscard]] constexpr InchesMercury inches_mercury_from_pascals(Pascals pascals) noexcept {
  return {pascals.value / kPascalsPerInchMercury};
}

[[nodiscard]] constexpr Radians radians_from_degrees(Degrees degrees) noexcept {
  return {degrees.value * kPi / 180.0};
}

[[nodiscard]] constexpr Degrees degrees_from_radians(Radians radians) noexcept {
  return {radians.value * 180.0 / kPi};
}

[[nodiscard]] constexpr double degrees_to_radians(double degrees) noexcept {
  return radians_from_degrees(Degrees{degrees}).value;
}

[[nodiscard]] constexpr double radians_to_degrees(double radians) noexcept {
  return degrees_from_radians(Radians{radians}).value;
}

} // namespace flying::geo_terrain::units
