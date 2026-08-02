#include "flying/geo_terrain/geodesy.hpp"
#include "flying/geo_terrain/heights.hpp"
#include "flying/geo_terrain/units.hpp"

#include <cmath>
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
using flying::geo_terrain::anchor_in_frame;
using flying::geo_terrain::body_vector_from_ecef;
using flying::geo_terrain::ecef_from_enu;
using flying::geo_terrain::ecef_from_ned;
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
using flying::geo_terrain::ned_from_ecef_position;
using flying::geo_terrain::orthometric_from_ellipsoidal;
using flying::geo_terrain::qfe_from_hectopascals;
using flying::geo_terrain::qnh_from_hectopascals;
using flying::geo_terrain::reanchor_in_frame;
using flying::geo_terrain::units::Degrees;
using flying::geo_terrain::units::Feet;
using flying::geo_terrain::units::Hectopascals;
using flying::geo_terrain::units::InchesMercury;
using flying::geo_terrain::units::Knots;
using flying::geo_terrain::units::Meters;
using flying::geo_terrain::units::MetersPerSecond;
using flying::geo_terrain::units::NauticalMiles;
using flying::geo_terrain::units::Pascals;
using flying::geo_terrain::units::degrees_from_radians;
using flying::geo_terrain::units::feet_from_meters;
using flying::geo_terrain::units::hectopascals_from_pascals;
using flying::geo_terrain::units::inches_mercury_from_pascals;
using flying::geo_terrain::units::knots_from_meters_per_second;
using flying::geo_terrain::units::meters_from_feet;
using flying::geo_terrain::units::meters_from_nautical_miles;
using flying::geo_terrain::units::meters_per_second_from_knots;
using flying::geo_terrain::units::nautical_miles_from_meters;
using flying::geo_terrain::units::pascals_from_hectopascals;
using flying::geo_terrain::units::pascals_from_inches_mercury;
using flying::geo_terrain::units::radians_from_degrees;

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

void geodetic_to_ecef_matches_wgs84_axis_references() {
  const auto equator_greenwich = geodetic_to_ecef(make_geodetic_degrees(0.0, 0.0, {0.0}));
  require_near(equator_greenwich.meters.x, Wgs84Ellipsoid::semi_major_axis_m, 1.0e-9,
               "equator and Greenwich should be on WGS-84 semi-major x axis");
  require_near(equator_greenwich.meters.y, 0.0, 1.0e-9,
               "equator and Greenwich should have zero y");
  require_near(equator_greenwich.meters.z, 0.0, 1.0e-9,
               "equator and Greenwich should have zero z");

  const auto equator_east = geodetic_to_ecef(make_geodetic_degrees(0.0, 90.0, {0.0}));
  require_near(equator_east.meters.x, 0.0, 1.0e-8,
               "equator and 90E should have zero x");
  require_near(equator_east.meters.y, Wgs84Ellipsoid::semi_major_axis_m, 1.0e-9,
               "equator and 90E should be on WGS-84 semi-major y axis");
  require_near(equator_east.meters.z, 0.0, 1.0e-9,
               "equator and 90E should have zero z");

  const auto north_pole = geodetic_to_ecef(make_geodetic_degrees(90.0, 17.0, {0.0}));
  require_near(north_pole.meters.x, 0.0, 1.0e-8,
               "north pole should have numerically zero x");
  require_near(north_pole.meters.y, 0.0, 1.0e-8,
               "north pole should have numerically zero y");
  require_near(north_pole.meters.z, Wgs84Ellipsoid::semi_minor_axis_m, 1.0e-6,
               "north pole should be on WGS-84 semi-minor z axis");
}

void geodetic_ecef_round_trip_matches_checked_reference_values() {
  const auto prague = make_geodetic_degrees(50.0755381, 14.4378005, {235.0});
  const auto ecef = geodetic_to_ecef(prague);

  require_near(ecef.meters.x, 3'972'042.642237056, 1.0e-6,
               "Prague reference ECEF x should match checked WGS-84 value");
  require_near(ecef.meters.y, 1'022'640.968997103, 1.0e-6,
               "Prague reference ECEF y should match checked WGS-84 value");
  require_near(ecef.meters.z, 4'868'365.770257858, 1.0e-6,
               "Prague reference ECEF z should match checked WGS-84 value");

  const auto round_trip = ecef_to_geodetic(ecef);
  require_near(round_trip.latitude_degrees(), 50.0755381, 1.0e-10,
               "ECEF to geodetic should preserve Prague latitude");
  require_near(round_trip.longitude_degrees(), 14.4378005, 1.0e-10,
               "ECEF to geodetic should preserve Prague longitude");
  require_near(round_trip.ellipsoidal_height.meters, 235.0, 1.0e-6,
               "ECEF to geodetic should preserve ellipsoidal height");
}

void local_enu_ned_frames_match_equator_reference_values() {
  const auto frame = make_local_tangent_frame(make_geodetic_degrees(0.0, 0.0, {0.0}));
  const auto from_enu = ecef_from_enu(frame, {100.0, 200.0, 50.0});

  require_near(from_enu.meters.x, Wgs84Ellipsoid::semi_major_axis_m + 50.0, 1.0e-9,
               "ENU up should align with ECEF +x at the equator origin");
  require_near(from_enu.meters.y, 100.0, 1.0e-9,
               "ENU east should align with ECEF +y at the equator origin");
  require_near(from_enu.meters.z, 200.0, 1.0e-9,
               "ENU north should align with ECEF +z at the equator origin");

  const auto enu = enu_from_ecef_position(frame, from_enu);
  require_near(enu.east_m, 100.0, 1.0e-9, "ENU east round trip should match");
  require_near(enu.north_m, 200.0, 1.0e-9, "ENU north round trip should match");
  require_near(enu.up_m, 50.0, 1.0e-9, "ENU up round trip should match");

  const auto from_ned = ecef_from_ned(frame, {200.0, 100.0, -50.0});
  require_near(from_ned.meters.x, from_enu.meters.x, 1.0e-9,
               "NED down should be the inverse of ENU up");
  require_near(from_ned.meters.y, from_enu.meters.y, 1.0e-9,
               "NED east should match ENU east");
  require_near(from_ned.meters.z, from_enu.meters.z, 1.0e-9,
               "NED north should match ENU north");

  const auto ned = ned_from_ecef_position(frame, from_ned);
  require_near(ned.north_m, 200.0, 1.0e-9, "NED north round trip should match");
  require_near(ned.east_m, 100.0, 1.0e-9, "NED east round trip should match");
  require_near(ned.down_m, -50.0, 1.0e-9, "NED down round trip should match");
}

void local_origin_shift_preserves_authoritative_ecef() {
  const auto old_frame = make_local_tangent_frame(make_geodetic_degrees(50.0755381, 14.4378005, {235.0}));
  const auto new_frame = make_local_tangent_frame(make_geodetic_degrees(50.0757000, 14.4381000, {237.0}));
  const auto aircraft_ecef = ecef_from_enu(old_frame, {12.5, -8.0, 2.0});

  const auto old_local = anchor_in_frame(old_frame, aircraft_ecef);
  const auto shifted = reanchor_in_frame(new_frame, old_local);

  require(shifted.authoritative_ecef.meters.x == aircraft_ecef.meters.x,
          "origin shift must copy authoritative ECEF x exactly");
  require(shifted.authoritative_ecef.meters.y == aircraft_ecef.meters.y,
          "origin shift must copy authoritative ECEF y exactly");
  require(shifted.authoritative_ecef.meters.z == aircraft_ecef.meters.z,
          "origin shift must copy authoritative ECEF z exactly");

  const auto reconstructed = ecef_from_enu(new_frame, shifted.local_enu);
  require_near(reconstructed.meters.x, aircraft_ecef.meters.x, 1.0e-8,
               "re-anchored local ENU should reconstruct authoritative ECEF x");
  require_near(reconstructed.meters.y, aircraft_ecef.meters.y, 1.0e-8,
               "re-anchored local ENU should reconstruct authoritative ECEF y");
  require_near(reconstructed.meters.z, aircraft_ecef.meters.z, 1.0e-8,
               "re-anchored local ENU should reconstruct authoritative ECEF z");
}

void body_frame_rotations_match_aviation_conventions() {
  const BodyVector forward{1.0, 0.0, 0.0};
  const BodyVector right{0.0, 1.0, 0.0};
  const NedVector north{1.0, 0.0, 0.0};

  const auto zero = make_body_euler_degrees(0.0, 0.0, 0.0);
  const auto zero_forward = ned_from_body_vector(zero, forward);
  require_near(zero_forward.north_m, 1.0, 1.0e-12, "zero attitude forward should point north");
  require_near(zero_forward.east_m, 0.0, 1.0e-12, "zero attitude forward should have no east");
  require_near(zero_forward.down_m, 0.0, 1.0e-12, "zero attitude forward should have no down");

  const auto yaw_east = ned_from_body_vector(make_body_euler_degrees(0.0, 0.0, 90.0), forward);
  require_near(yaw_east.north_m, 0.0, 1.0e-12, "90 degree yaw should remove north component");
  require_near(yaw_east.east_m, 1.0, 1.0e-12, "90 degree yaw should point forward east");
  require_near(yaw_east.down_m, 0.0, 1.0e-12, "90 degree yaw should remain level");

  const auto pitch_up = ned_from_body_vector(make_body_euler_degrees(0.0, 90.0, 0.0), forward);
  require_near(pitch_up.north_m, 0.0, 1.0e-12, "90 degree pitch should remove north component");
  require_near(pitch_up.east_m, 0.0, 1.0e-12, "90 degree pitch should have no east");
  require_near(pitch_up.down_m, -1.0, 1.0e-12, "positive pitch should point the nose up");

  const auto roll_right = ned_from_body_vector(make_body_euler_degrees(90.0, 0.0, 0.0), right);
  require_near(roll_right.north_m, 0.0, 1.0e-12, "90 degree roll should not move right wing north");
  require_near(roll_right.east_m, 0.0, 1.0e-12, "90 degree roll should remove right-wing east");
  require_near(roll_right.down_m, 1.0, 1.0e-12, "positive roll should move right wing down");

  const auto recovered_body = flying::geo_terrain::body_from_ned_vector(zero, north);
  require_near(recovered_body.forward, 1.0, 1.0e-12, "NED north should map back to body forward");

  const auto equator_frame = make_local_tangent_frame(make_geodetic_degrees(0.0, 0.0, {0.0}));
  const auto ecef_forward = ecef_vector_from_body(equator_frame, zero, forward);
  require_near(ecef_forward.meters.x, 0.0, 1.0e-12,
               "body forward at equator origin should have no ECEF x");
  require_near(ecef_forward.meters.y, 0.0, 1.0e-12,
               "body forward at equator origin should have no ECEF y");
  require_near(ecef_forward.meters.z, 1.0, 1.0e-12,
               "body forward at equator origin should align with ECEF +z");

  const auto recovered_from_ecef = body_vector_from_ecef(equator_frame, zero, ecef_forward);
  require_near(recovered_from_ecef.forward, 1.0, 1.0e-12,
               "body/ECEF/body transform should preserve forward component");
}

void height_value_objects_keep_height_datums_separate() {
  const OrthometricHeight orthometric{250.0};
  const GeoidUndulation geoid{45.0};
  const auto ellipsoidal = ellipsoidal_from_orthometric(orthometric, geoid);
  const auto recovered_orthometric = orthometric_from_ellipsoidal(ellipsoidal, geoid);

  require_near(ellipsoidal.meters, 295.0, 0.0,
               "ellipsoidal height should equal orthometric height plus geoid undulation");
  require_near(recovered_orthometric.meters, 250.0, 0.0,
               "orthometric height should equal ellipsoidal height minus geoid undulation");

  const PressureAltitude pressure_altitude{1'500.0};
  require_near(pressure_altitude.meters, 1'500.0, 0.0,
               "pressure altitude should be an independent value object");

  const auto qnh = qnh_from_hectopascals(1013.25);
  const auto qfe = qfe_from_hectopascals(996.0);
  require_near(qnh.pressure_pa, 101'325.0, 0.0, "QNH hPa should convert to Pa");
  require_near(qfe.pressure_pa, 99'600.0, 0.0, "QFE hPa should convert to Pa");

  const auto indicated_qnh = indicated_altitude_qnh(1'250.0);
  const auto indicated_qfe = indicated_altitude_qfe(75.0);
  const auto indicated_std = indicated_altitude_standard_pressure(1'500.0);
  require(indicated_qnh.datum == IndicatedAltitudeDatum::kQnh,
          "indicated altitude should identify QNH datum");
  require(indicated_qfe.datum == IndicatedAltitudeDatum::kQfe,
          "indicated altitude should identify QFE datum");
  require(indicated_std.datum == IndicatedAltitudeDatum::kStandardPressure,
          "indicated altitude should identify standard pressure datum");
}

void unit_conversions_match_reference_values() {
  require_near(meters_from_feet(Feet{1.0}).value, 0.3048, 0.0,
               "one international foot should be exactly 0.3048 m");
  require_near(feet_from_meters(Meters{0.3048}).value, 1.0, 0.0,
               "0.3048 m should be one international foot");
  require_near(meters_from_nautical_miles(NauticalMiles{1.0}).value, 1852.0, 0.0,
               "one nautical mile should be exactly 1852 m");
  require_near(nautical_miles_from_meters(Meters{1852.0}).value, 1.0, 0.0,
               "1852 m should be one nautical mile");
  require_near(meters_per_second_from_knots(Knots{1.0}).value, 0.5144444444444445, 1.0e-16,
               "one knot should be one nautical mile per hour in m/s");
  require_near(knots_from_meters_per_second(MetersPerSecond{0.5144444444444445}).value, 1.0, 1.0e-15,
               "one nautical mile per hour in m/s should be one knot");
  require_near(pascals_from_hectopascals(Hectopascals{1013.25}).value, 101'325.0, 0.0,
               "1013.25 hPa should be standard sea-level pressure");
  require_near(hectopascals_from_pascals(Pascals{101'325.0}).value, 1013.25, 0.0,
               "standard sea-level pressure should convert to 1013.25 hPa");
  require_near(pascals_from_inches_mercury(InchesMercury{1.0}).value, 3386.389, 0.0,
               "one inch of mercury should match the project boundary constant");
  require_near(inches_mercury_from_pascals(Pascals{101'325.0}).value, 29.92125240189476, 1.0e-12,
               "standard sea-level pressure should convert to the checked inHg value");
  require_near(radians_from_degrees(Degrees{180.0}).value, flying::geo_terrain::kPi, 0.0,
               "180 degrees should be pi radians");
  require_near(degrees_from_radians({flying::geo_terrain::kPi}).value, 180.0, 0.0,
               "pi radians should be 180 degrees");
}

} // namespace

int main() {
  geodetic_to_ecef_matches_wgs84_axis_references();
  geodetic_ecef_round_trip_matches_checked_reference_values();
  local_enu_ned_frames_match_equator_reference_values();
  local_origin_shift_preserves_authoritative_ecef();
  body_frame_rotations_match_aviation_conventions();
  height_value_objects_keep_height_datums_separate();
  unit_conversions_match_reference_values();
  return 0;
}
