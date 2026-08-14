#include "Geodesy.h"
#include "Units.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {

using flying::geo_terrain::BodyVector;
using flying::geo_terrain::EllipsoidalHeight;
using flying::geo_terrain::GeoidUndulation;
using flying::geo_terrain::IndicatedAltitudeDatum;
using flying::geo_terrain::NedVector;
using flying::geo_terrain::OrthometricHeight;
using flying::geo_terrain::PressureAltitude;
using flying::geo_terrain::QfeSetting;
using flying::geo_terrain::QnhSetting;
using flying::geo_terrain::Wgs84Ellipsoid;
using flying::geo_terrain::body_vector_from_ecef;
using flying::geo_terrain::ecef_from_enu;
using flying::geo_terrain::ecef_to_geodetic;
using flying::geo_terrain::ecef_vector_from_body;
using flying::geo_terrain::ellipsoidal_from_orthometric;
using flying::geo_terrain::enu_from_ecef_position;
using flying::geo_terrain::geodetic_to_ecef;
using flying::geo_terrain::indicated_altitude_qfe;
using flying::geo_terrain::indicated_altitude_qnh;
using flying::geo_terrain::indicated_altitude_standard_pressure;
using flying::geo_terrain::make_body_euler_degrees;
using flying::geo_terrain::make_geodetic_degrees;
using flying::geo_terrain::make_local_tangent_frame;
using flying::geo_terrain::ned_from_body_vector;
using flying::geo_terrain::orthometric_from_ellipsoidal;
using flying::geo_terrain::qfe_from_hectopascals;
using flying::geo_terrain::qnh_from_hectopascals;
using flying::core_sim::units::Feet;
using flying::core_sim::units::Hectopascals;
using flying::core_sim::units::Knots;
using flying::core_sim::units::Meters;
using flying::core_sim::units::MetersPerSecond;
using flying::core_sim::units::NauticalMiles;
using flying::core_sim::units::Pascals;
using flying::core_sim::units::feet_from_meters;
using flying::core_sim::units::hectopascals_from_pascals;
using flying::core_sim::units::knots_from_meters_per_second;
using flying::core_sim::units::meters_from_feet;
using flying::core_sim::units::meters_from_nautical_miles;
using flying::core_sim::units::meters_per_second_from_knots;
using flying::core_sim::units::nautical_miles_from_meters;
using flying::core_sim::units::pascals_from_hectopascals;

static_assert(std::is_same_v<decltype(EllipsoidalHeight{}.meters), double>);
static_assert(std::is_same_v<decltype(OrthometricHeight{}.meters), double>);
static_assert(std::is_same_v<decltype(PressureAltitude{}.meters), double>);
static_assert(std::is_same_v<decltype(QnhSetting{}.pressure_pa), double>);
static_assert(std::is_same_v<decltype(QfeSetting{}.pressure_pa), double>);
static_assert(!std::is_convertible_v<OrthometricHeight, EllipsoidalHeight>);
static_assert(!std::is_convertible_v<EllipsoidalHeight, OrthometricHeight>);

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

void wgs84_ecef_reference_vectors_match() {
  const auto prague = make_geodetic_degrees(50.0755381, 14.4378005, {235.0});
  const auto ecef = geodetic_to_ecef(prague);

  require_near(ecef.meters.x, 3'972'042.642237056, 1.0e-6, "Prague ECEF x reference mismatch");
  require_near(ecef.meters.y, 1'022'640.968997103, 1.0e-6, "Prague ECEF y reference mismatch");
  require_near(ecef.meters.z, 4'868'365.770257858, 1.0e-6, "Prague ECEF z reference mismatch");

  const auto round_trip = ecef_to_geodetic(ecef);
  require_near(round_trip.latitude_degrees(), 50.0755381, 1.0e-10, "latitude round trip mismatch");
  require_near(round_trip.longitude_degrees(), 14.4378005, 1.0e-10, "longitude round trip mismatch");
  require_near(round_trip.ellipsoidal_height.meters, 235.0, 1.0e-6, "height round trip mismatch");

  const auto equator = geodetic_to_ecef(make_geodetic_degrees(0.0, 0.0, {0.0}));
  require_near(equator.meters.x, Wgs84Ellipsoid::semi_major_axis_m, 1.0e-9, "equator x mismatch");
  require_near(equator.meters.y, 0.0, 1.0e-9, "equator y mismatch");
  require_near(equator.meters.z, 0.0, 1.0e-9, "equator z mismatch");
  std::cout << "PASS wgs84_ecef_reference_vectors_match\n";
}

void local_and_body_frames_round_trip() {
  const auto frame = make_local_tangent_frame(make_geodetic_degrees(0.0, 0.0, {0.0}));
  const auto ecef = ecef_from_enu(frame, {100.0, 200.0, 50.0});
  const auto enu = enu_from_ecef_position(frame, ecef);

  require_near(enu.east_m, 100.0, 1.0e-9, "ENU east round trip mismatch");
  require_near(enu.north_m, 200.0, 1.0e-9, "ENU north round trip mismatch");
  require_near(enu.up_m, 50.0, 1.0e-9, "ENU up round trip mismatch");

  const BodyVector forward{1.0, 0.0, 0.0};
  const NedVector north{1.0, 0.0, 0.0};
  const auto zero_attitude = make_body_euler_degrees(0.0, 0.0, 0.0);
  const auto yaw_east = ned_from_body_vector(make_body_euler_degrees(0.0, 0.0, 90.0), forward);
  require_near(yaw_east.east_m, 1.0, 1.0e-12, "90 degree yaw should point body forward east");

  const auto ecef_forward = ecef_vector_from_body(frame, zero_attitude, forward);
  const auto recovered = body_vector_from_ecef(frame, zero_attitude, ecef_forward);
  require_near(recovered.forward, north.north_m, 1.0e-12, "body/ECEF/body round trip mismatch");
  std::cout << "PASS local_and_body_frames_round_trip\n";
}

void altitude_datums_are_explicit() {
  const auto ellipsoidal = ellipsoidal_from_orthometric(OrthometricHeight{250.0}, GeoidUndulation{45.0});
  const auto orthometric = orthometric_from_ellipsoidal(ellipsoidal, GeoidUndulation{45.0});
  require_near(ellipsoidal.meters, 295.0, 0.0, "ellipsoidal height datum conversion mismatch");
  require_near(orthometric.meters, 250.0, 0.0, "orthometric height datum conversion mismatch");

  const PressureAltitude pressure_altitude{1'500.0};
  require_near(pressure_altitude.meters, 1'500.0, 0.0, "pressure altitude value mismatch");
  require_near(qnh_from_hectopascals(1013.25).pressure_pa, 101'325.0, 0.0, "QNH conversion mismatch");
  require_near(qfe_from_hectopascals(996.0).pressure_pa, 99'600.0, 0.0, "QFE conversion mismatch");
  require(indicated_altitude_qnh(1'250.0).datum == IndicatedAltitudeDatum::kQnh, "QNH indicated datum mismatch");
  require(indicated_altitude_qfe(75.0).datum == IndicatedAltitudeDatum::kQfe, "QFE indicated datum mismatch");
  require(indicated_altitude_standard_pressure(1'500.0).datum == IndicatedAltitudeDatum::kStandardPressure,
          "standard-pressure indicated datum mismatch");
  std::cout << "PASS altitude_datums_are_explicit\n";
}

void core_sim_boundary_units_match_references() {
  require_near(meters_from_feet(Feet{1.0}).value, 0.3048, 0.0, "foot conversion mismatch");
  require_near(feet_from_meters(Meters{0.3048}).value, 1.0, 0.0, "meter to foot conversion mismatch");
  require_near(meters_from_nautical_miles(NauticalMiles{1.0}).value, 1852.0, 0.0, "NM conversion mismatch");
  require_near(nautical_miles_from_meters(Meters{1852.0}).value, 1.0, 0.0, "meter to NM conversion mismatch");
  require_near(meters_per_second_from_knots(Knots{1.0}).value, 0.5144444444444445, 1.0e-16,
               "knot conversion mismatch");
  require_near(knots_from_meters_per_second(MetersPerSecond{0.5144444444444445}).value, 1.0, 1.0e-15,
               "meter per second to knot conversion mismatch");
  require_near(pascals_from_hectopascals(Hectopascals{1013.25}).value, 101'325.0, 0.0,
               "hPa conversion mismatch");
  require_near(hectopascals_from_pascals(Pascals{101'325.0}).value, 1013.25, 0.0,
               "Pa to hPa conversion mismatch");
  std::cout << "PASS core_sim_boundary_units_match_references\n";
}

} // namespace

int main() {
  wgs84_ecef_reference_vectors_match();
  local_and_body_frames_round_trip();
  altitude_datums_are_explicit();
  core_sim_boundary_units_match_references();
  return 0;
}
