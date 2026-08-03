#include "flying/core_sim/scenario.hpp"

#include "flying/geo_terrain/geodesy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace flying::core_sim {
namespace {

using flying::geo_terrain::BodyEulerAngles;
using flying::geo_terrain::BodyVector;
using flying::geo_terrain::EcefPosition;
using flying::geo_terrain::EnuVector;
using flying::geo_terrain::GeodeticCoordinates;
using flying::geo_terrain::LocalTangentFrame;

constexpr double kOnGroundHeightOffsetM = 1.5;
constexpr double kAirborneHeightAglM = 450.0;
constexpr double kAirborneSpeedMps = 35.0;

[[nodiscard]] bool is_finite(double value) noexcept {
  return std::isfinite(value);
}

[[nodiscard]] GeodeticCoordinates make_geodetic(double latitude_deg,
                                                double longitude_deg,
                                                double height_m) noexcept {
  return flying::geo_terrain::make_geodetic_degrees(
      latitude_deg,
      longitude_deg,
      flying::geo_terrain::EllipsoidalHeight{height_m});
}

[[nodiscard]] Quaterniond quaternion_from_rotation_matrix(double r00,
                                                          double r01,
                                                          double r02,
                                                          double r10,
                                                          double r11,
                                                          double r12,
                                                          double r20,
                                                          double r21,
                                                          double r22) noexcept {
  const double trace = r00 + r11 + r22;
  Quaterniond q{};
  if (trace > 0.0) {
    const double s = std::sqrt(trace + 1.0) * 2.0;
    q.w = 0.25 * s;
    q.x = (r21 - r12) / s;
    q.y = (r02 - r20) / s;
    q.z = (r10 - r01) / s;
  } else if (r00 > r11 && r00 > r22) {
    const double s = std::sqrt(1.0 + r00 - r11 - r22) * 2.0;
    q.w = (r21 - r12) / s;
    q.x = 0.25 * s;
    q.y = (r01 + r10) / s;
    q.z = (r02 + r20) / s;
  } else if (r11 > r22) {
    const double s = std::sqrt(1.0 + r11 - r00 - r22) * 2.0;
    q.w = (r02 - r20) / s;
    q.x = (r01 + r10) / s;
    q.y = 0.25 * s;
    q.z = (r12 + r21) / s;
  } else {
    const double s = std::sqrt(1.0 + r22 - r00 - r11) * 2.0;
    q.w = (r10 - r01) / s;
    q.x = (r02 + r20) / s;
    q.y = (r12 + r21) / s;
    q.z = 0.25 * s;
  }

  return q.normalized();
}

[[nodiscard]] Quaterniond make_body_to_ecef(LocalTangentFrame frame, double heading_rad) noexcept {
  const BodyEulerAngles body_to_ned{0.0, 0.0, heading_rad};
  const auto forward =
      flying::geo_terrain::ecef_vector_from_body(frame, body_to_ned, BodyVector{1.0, 0.0, 0.0});
  const auto right =
      flying::geo_terrain::ecef_vector_from_body(frame, body_to_ned, BodyVector{0.0, 1.0, 0.0});
  const auto down =
      flying::geo_terrain::ecef_vector_from_body(frame, body_to_ned, BodyVector{0.0, 0.0, 1.0});

  return quaternion_from_rotation_matrix(
      forward.meters.x,
      right.meters.x,
      down.meters.x,
      forward.meters.y,
      right.meters.y,
      down.meters.y,
      forward.meters.z,
      right.meters.z,
      down.meters.z);
}

[[nodiscard]] const PilotScenarioLocation& find_location(
    std::string_view location_id,
    std::span<const PilotScenarioLocation> locations) {
  const auto found = std::find_if(
      locations.begin(),
      locations.end(),
      [&](const PilotScenarioLocation& location) {
        return location.location_id == location_id && location.selectable;
      });

  if (found == locations.end()) {
    throw std::invalid_argument("scenario location is not in the selectable pilot-region catalog");
  }

  return *found;
}

void validate_location(const PilotScenarioLocation& location) {
  if (location.location_id.empty()) {
    throw std::invalid_argument("scenario location_id must not be empty");
  }
  if (!is_finite(location.latitude_deg) ||
      location.latitude_deg < -90.0 ||
      location.latitude_deg > 90.0) {
    throw std::invalid_argument("scenario latitude_deg must be finite and in [-90, 90]");
  }
  if (!is_finite(location.longitude_deg) ||
      location.longitude_deg < -180.0 ||
      location.longitude_deg > 180.0) {
    throw std::invalid_argument("scenario longitude_deg must be finite and in [-180, 180]");
  }
  if (!is_finite(location.elevation_m) || !is_finite(location.true_heading_deg)) {
    throw std::invalid_argument("scenario location elevation and heading must be finite");
  }
}

[[nodiscard]] double start_mode_height_offset(ScenarioStartMode mode) noexcept {
  return mode == ScenarioStartMode::Airborne ? kAirborneHeightAglM : kOnGroundHeightOffsetM;
}

[[nodiscard]] double start_mode_speed(ScenarioStartMode mode) noexcept {
  return mode == ScenarioStartMode::Airborne ? kAirborneSpeedMps : 0.0;
}

[[nodiscard]] AircraftControlInputSample initial_controls_for(ScenarioStartMode mode) noexcept {
  AircraftControlInputSample controls{};
  switch (mode) {
  case ScenarioStartMode::ColdAndDark:
    controls.throttle_norm = 0.0;
    controls.mixture_norm = 0.0;
    controls.propeller_norm = 0.0;
    break;
  case ScenarioStartMode::ReadyToTaxi:
    controls.throttle_norm = 0.05;
    controls.mixture_norm = 1.0;
    controls.propeller_norm = 1.0;
    break;
  case ScenarioStartMode::Airborne:
    controls.throttle_norm = 0.72;
    controls.mixture_norm = 1.0;
    controls.propeller_norm = 1.0;
    break;
  }

  return controls;
}

[[nodiscard]] ScenarioInitialState build_initial_state(const ScenarioSelection& selection,
                                                       const PilotScenarioLocation& location) {
  validate_location(location);

  const double height_m = location.elevation_m + start_mode_height_offset(selection.start_mode);
  const double speed_mps = start_mode_speed(selection.start_mode);
  const double heading_rad = flying::geo_terrain::units::degrees_to_radians(location.true_heading_deg);
  const GeodeticCoordinates geodetic =
      make_geodetic(location.latitude_deg, location.longitude_deg, height_m);
  const EcefPosition ecef = flying::geo_terrain::geodetic_to_ecef(geodetic);
  const LocalTangentFrame frame = flying::geo_terrain::make_local_tangent_frame(geodetic);
  const EnuVector velocity_enu{
    std::sin(heading_rad) * speed_mps,
    std::cos(heading_rad) * speed_mps,
    0.0,
  };
  const auto ecef_velocity = flying::geo_terrain::ecef_vector_from_enu(frame, velocity_enu);

  AuthoritativeState rigid{};
  rigid.ecef_position_m = {ecef.meters.x, ecef.meters.y, ecef.meters.z};
  rigid.ecef_velocity_mps = {
    ecef_velocity.meters.x,
    ecef_velocity.meters.y,
    ecef_velocity.meters.z,
  };
  rigid.body_to_ecef = make_body_to_ecef(frame, heading_rad);

  FlightDynamicsInitialCondition flight{};
  flight.latitude_deg = location.latitude_deg;
  flight.longitude_deg = location.longitude_deg;
  flight.altitude_m = height_m;
  flight.terrain_elevation_m = location.elevation_m;
  flight.body_velocity_mps = {speed_mps, 0.0, 0.0};
  flight.heading_rad = heading_rad;

  ScenarioInitialState state{};
  state.selection = selection;
  state.location = location;
  state.rigid_body_state = rigid;
  state.flight_dynamics_initial_condition = flight;
  state.initial_controls = initial_controls_for(selection.start_mode);
  state.battery_on = selection.start_mode != ScenarioStartMode::ColdAndDark;
  state.engine_running = selection.start_mode != ScenarioStartMode::ColdAndDark;
  state.avionics_on = selection.start_mode == ScenarioStartMode::Airborne;
  state.parking_brake_set = selection.start_mode == ScenarioStartMode::ColdAndDark;
  return state;
}

} // namespace

std::string_view to_string(ScenarioStartMode value) noexcept {
  switch (value) {
  case ScenarioStartMode::ColdAndDark:
    return "cold_and_dark";
  case ScenarioStartMode::ReadyToTaxi:
    return "ready_to_taxi";
  case ScenarioStartMode::Airborne:
    return "airborne";
  }

  return "unknown";
}

ScenarioStartMode scenario_start_mode_from_string(std::string_view value) {
  if (value == "cold_and_dark") {
    return ScenarioStartMode::ColdAndDark;
  }
  if (value == "ready_to_taxi") {
    return ScenarioStartMode::ReadyToTaxi;
  }
  if (value == "airborne") {
    return ScenarioStartMode::Airborne;
  }

  throw std::invalid_argument("scenario start mode is unsupported");
}

std::vector<PilotScenarioLocation> default_pilot_scenario_locations() {
  return {
    {
      "FPPV-RWY-09",
      "FPPV",
      "09",
      "Flying Pilot Paved Airport runway 09",
      49.2,
      14.4942,
      429.2,
      90.0,
      true,
    },
    {
      "FPPV-RWY-27",
      "FPPV",
      "27",
      "Flying Pilot Paved Airport runway 27",
      49.2,
      14.5058,
      430.9,
      270.0,
      true,
    },
    {
      "FPGS-RWY-16",
      "FPGS",
      "16",
      "Flying Pilot Grass SLZ Field runway 16",
      49.2083,
      14.5146,
      420.4,
      160.0,
      true,
    },
    {
      "FPGS-RWY-34",
      "FPGS",
      "34",
      "Flying Pilot Grass SLZ Field runway 34",
      49.2037,
      14.5174,
      423.6,
      340.0,
      true,
    },
  };
}

ScenarioInitialState make_scenario_initial_state(const ScenarioSelection& selection) {
  const std::vector<PilotScenarioLocation> locations = default_pilot_scenario_locations();
  return make_scenario_initial_state(selection, locations);
}

ScenarioInitialState make_scenario_initial_state(
    const ScenarioSelection& selection,
    std::span<const PilotScenarioLocation> locations) {
  return build_initial_state(selection, find_location(selection.location_id, locations));
}

ScenarioInitialState reset_simulator_to_scenario(CoreSimulator& simulator,
                                                 const ScenarioSelection& selection) {
  const std::vector<PilotScenarioLocation> locations = default_pilot_scenario_locations();
  return reset_simulator_to_scenario(simulator, selection, locations);
}

ScenarioInitialState reset_simulator_to_scenario(
    CoreSimulator& simulator,
    const ScenarioSelection& selection,
    std::span<const PilotScenarioLocation> locations) {
  ScenarioInitialState initial_state = make_scenario_initial_state(selection, locations);
  simulator.reset(initial_state.rigid_body_state,
                  initial_state.flight_dynamics_initial_condition,
                  initial_state.initial_controls);
  return initial_state;
}

} // namespace flying::core_sim
