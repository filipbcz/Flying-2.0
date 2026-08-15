#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/jsbsim_adapter.hpp"
#include "flying/core_sim/terrain_contact.hpp"
#include "flying/geo_terrain/terrain_service.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr double kGravityMps2 = 9.80665;
constexpr double kMetersToFeet = 3.280839895013123;
constexpr double kKnotsToMetersPerSecond = 0.5144444444444445;
constexpr double kIsaSeaLevelTemperatureK = 288.15;
constexpr double kIsaLapseRateKPerM = 0.0065;
constexpr double kIsaGamma = 1.4;
constexpr double kAirGasConstant = 287.05287;

using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::FlightDynamicsInitialCondition;
using flying::core_sim::FlightDynamicsState;
using flying::core_sim::FlightDynamicsStepper;
using flying::core_sim::JsbsimFlightDynamicsBackend;
using flying::core_sim::TerrainContactQuery;
using flying::core_sim::Vector3d;
using flying::core_sim::kFixedStepSeconds;
using flying::core_sim::query_terrain_contact;
using flying::geo_terrain::EnuVector;
using flying::geo_terrain::InMemoryDemSurface;
using flying::geo_terrain::InMemoryTerrainHeightService;
using flying::geo_terrain::OrthometricHeight;
using flying::geo_terrain::TerrainCollisionMetadata;
using flying::geo_terrain::TerrainConfidenceMetadata;
using flying::geo_terrain::TerrainLocalBounds;
using flying::geo_terrain::TerrainSurfaceMaterial;
using flying::geo_terrain::make_flat_terrain_plane;
using flying::geo_terrain::make_geodetic_degrees;
using flying::geo_terrain::make_sloped_terrain_plane;

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

void require_greater_than(double actual, double expected, const char* message) {
  if (actual <= expected) {
    throw std::runtime_error(std::string(message) + " actual=" + std::to_string(actual) +
                             " expected-greater-than=" + std::to_string(expected));
  }
}

void require_less_than(double actual, double expected, const std::string& message) {
  if (actual >= expected) {
    throw std::runtime_error(message + " actual=" + std::to_string(actual) +
                             " expected-less-than=" + std::to_string(expected));
  }
}

bool finite(Vector3d value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double norm(Vector3d value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

AircraftControlInputSample running_controls(double throttle_norm) {
  AircraftControlInputSample controls{};
  controls.throttle_norm = throttle_norm;
  controls.mixture_norm = 1.0;
  controls.propeller_norm = 1.0;
  controls.engine_run_switch = true;
  controls.magnetos_on = true;
  controls.engine_starter_engaged = false;
  return controls;
}

AircraftControlInputSample stopped_controls() {
  AircraftControlInputSample controls = running_controls(0.0);
  controls.engine_run_switch = false;
  controls.magnetos_on = false;
  controls.engine_starter_engaged = false;
  return controls;
}

std::pair<double, double> degrees_from_local_query(flying::geo_terrain::TerrainHeightQuery query) {
  const flying::geo_terrain::GeodeticCoordinates geodetic{
    query.latitude_rad,
    query.longitude_rad,
    {0.0},
  };
  return {geodetic.latitude_degrees(), geodetic.longitude_degrees()};
}

FlightDynamicsInitialCondition airborne_state(double pitch_rad,
                                             double roll_rad = 0.0,
                                             double body_vertical_mps = 0.0) {
  FlightDynamicsInitialCondition initial{};
  initial.latitude_deg = 49.2;
  initial.longitude_deg = 16.6;
  initial.altitude_m = 1'500.0;
  initial.terrain_elevation_m = 320.0;
  initial.body_velocity_mps = {62.0, 0.0, body_vertical_mps};
  initial.pitch_rad = pitch_rad;
  initial.roll_rad = roll_rad;
  return initial;
}

FlightDynamicsInitialCondition airborne_state_with_forward_speed(double pitch_rad,
                                                                 double forward_speed_mps,
                                                                 double roll_rad = 0.0) {
  FlightDynamicsInitialCondition initial = airborne_state(pitch_rad, roll_rad);
  initial.body_velocity_mps.x = forward_speed_mps;
  return initial;
}

void advance_for(FlightDynamicsStepper& stepper,
                 double seconds,
                 const AircraftControlInputSample& controls) {
  const int steps = static_cast<int>(std::round(seconds / kFixedStepSeconds));
  for (int step = 0; step < steps; ++step) {
    stepper.advance(kFixedStepSeconds, controls);
  }
}

void start_engine(FlightDynamicsStepper& stepper) {
  AircraftControlInputSample starting = running_controls(0.35);
  starting.engine_starter_engaged = true;
  advance_for(stepper, 1.0, starting);

  AircraftControlInputSample warm_idle = running_controls(0.20);
  advance_for(stepper, 0.5, warm_idle);
}

struct FlightSamplePair {
  FlightDynamicsState one_second_before_final{};
  FlightDynamicsState final{};
};

FlightSamplePair run_aircraft_sample_pair(double seconds,
                                          FlightDynamicsInitialCondition initial,
                                          AircraftControlInputSample controls) {
  FlightDynamicsStepper stepper{std::make_unique<JsbsimFlightDynamicsBackend>()};
  stepper.reset(initial);
  if (controls.engine_run_switch) {
    start_engine(stepper);
  }

  const int steps = static_cast<int>(std::ceil(seconds / kFixedStepSeconds));
  const int penultimate_sample_step = std::max(0, steps - static_cast<int>(std::round(1.0 / kFixedStepSeconds)));
  FlightDynamicsState one_second_before_final = stepper.state();
  for (int step = 0; step < steps; ++step) {
    stepper.advance(kFixedStepSeconds, controls);
    if (step + 1 == penultimate_sample_step) {
      one_second_before_final = stepper.state();
    }
  }

  return {one_second_before_final, stepper.state()};
}

FlightDynamicsState run_aircraft(double seconds,
                                 FlightDynamicsInitialCondition initial,
                                 AircraftControlInputSample controls) {
  return run_aircraft_sample_pair(seconds, initial, controls).final;
}

struct TrimTarget {
  const char* label{};
  double target_ned_vertical_mps{};
  double target_bank_rad{};
  double target_yaw_rate_radps{};
};

struct TrimCandidate {
  FlightDynamicsState one_second_before_final{};
  FlightDynamicsState final{};
  AircraftControlInputSample controls{};
  double pitch_rad{};
  double score{std::numeric_limits<double>::infinity()};
};

double speed_delta(const FlightDynamicsState& before, const FlightDynamicsState& after) {
  const Vector3d delta{
    after.ned_velocity_mps.x - before.ned_velocity_mps.x,
    after.ned_velocity_mps.y - before.ned_velocity_mps.y,
    after.ned_velocity_mps.z - before.ned_velocity_mps.z,
  };
  return norm(delta);
}

double trim_score(const TrimTarget& target,
                  const FlightDynamicsState& before,
                  const FlightDynamicsState& after) {
  const double vertical_error = std::abs(after.ned_velocity_mps.z - target.target_ned_vertical_mps);
  const double bank_error = std::abs(after.euler_rad.x - target.target_bank_rad);
  const double yaw_error = std::abs(after.angular_velocity_body_radps.z - target.target_yaw_rate_radps);
  return vertical_error * 5.0 + bank_error * 6.0 + yaw_error * 8.0 +
         norm(after.angular_velocity_body_radps) * 4.0 + speed_delta(before, after);
}

TrimCandidate solve_trim(const TrimTarget& target,
                         const std::vector<double>& pitch_candidates_rad,
                         const std::vector<double>& throttle_candidates,
                         const std::vector<double>& elevator_candidates,
                         const std::vector<double>& elevator_trim_candidates,
                         const std::vector<double>& aileron_candidates,
                         const std::vector<double>& rudder_candidates,
                         double initial_bank_rad = 0.0) {
  TrimCandidate best{};
  for (double pitch_rad : pitch_candidates_rad) {
    for (double throttle_norm : throttle_candidates) {
      for (double elevator_norm : elevator_candidates) {
        for (double elevator_trim_norm : elevator_trim_candidates) {
          for (double aileron_norm : aileron_candidates) {
            for (double rudder_norm : rudder_candidates) {
              AircraftControlInputSample controls{};
              controls.throttle_norm = throttle_norm;
              controls.mixture_norm = 1.0;
              controls.propeller_norm = 1.0;
              controls.elevator_norm = elevator_norm;
              controls.elevator_trim_norm = elevator_trim_norm;
              controls.aileron_norm = aileron_norm;
              controls.rudder_norm = rudder_norm;
              controls.engine_run_switch = true;
              controls.magnetos_on = true;
              controls.engine_starter_engaged = false;

              const FlightSamplePair sample =
                  run_aircraft_sample_pair(2.0, airborne_state(pitch_rad, initial_bank_rad), controls);
              const double score = trim_score(target, sample.one_second_before_final, sample.final);
              if (score < best.score) {
                best = {sample.one_second_before_final, sample.final, controls, pitch_rad, score};
              }
            }
          }
        }
      }
    }
  }
  return best;
}

void require_trim_equilibrium(const TrimTarget& target,
                              const TrimCandidate& candidate,
                              double vertical_tolerance_mps,
                              double speed_drift_tolerance_mps,
                              double angular_rate_tolerance_radps) {
  const FlightDynamicsState& state = candidate.final;
  require(state.step_index > 0, "trim candidate should advance the 240 Hz dynamics model");
  require(finite(state.body_velocity_mps), "trim velocity should remain finite");
  require(finite(state.total_force_body_n), "trim force balance should remain finite");
  require(finite(state.total_moment_body_nm), "trim moment balance should remain finite");
  require(std::abs(state.euler_rad.y) < 0.35,
          "trim candidate should remain within piston-trainer pitch attitude limits");
  require(std::abs(state.sideslip_rad) < 0.25,
          "trim candidate should remain inside coordinated-flight sideslip limits");
  require_less_than(std::abs(state.ned_velocity_mps.z - target.target_ned_vertical_mps),
                    vertical_tolerance_mps,
                    std::string(target.label) +
                        " trim should converge to the configured flight-path vertical speed pitch=" +
                        std::to_string(candidate.pitch_rad) + " throttle=" +
                        std::to_string(candidate.controls.throttle_norm) + " elevator=" +
                        std::to_string(candidate.controls.elevator_norm) + " elevator_trim=" +
                        std::to_string(candidate.controls.elevator_trim_norm));
  require_less_than(speed_delta(candidate.one_second_before_final, state),
                    speed_drift_tolerance_mps,
                    std::string(target.label) +
                        " trim should hold steady speed over the final sampled second");
  require_less_than(norm(state.angular_velocity_body_radps),
                    angular_rate_tolerance_radps,
                    std::string(target.label) +
                        " trim should converge to bounded steady angular rates");
}

double specific_energy(const FlightDynamicsState& state) {
  return kGravityMps2 * state.altitude_m + 0.5 * norm(state.body_velocity_mps) * norm(state.body_velocity_mps);
}

void primary_aircraft_package_is_present() {
#ifndef FLYING_REPO_SOURCE_DIR
#define FLYING_REPO_SOURCE_DIR "."
#endif
  const std::filesystem::path root{FLYING_REPO_SOURCE_DIR};
  require(std::filesystem::exists(root / "Data/Aircraft/PrimaryAircraft/manifest.json"),
          "primary aircraft package manifest should exist");
  require(std::filesystem::exists(root / "core_sim/jsbsim/aircraft/flying_trainer_one/flying_trainer_one.xml"),
          "primary JSBSim aircraft model should exist");
  require(std::filesystem::exists(root / "core_sim/jsbsim/aircraft/flying_trainer_one/reset00.xml"),
          "primary JSBSim reset state should exist");
  require(std::filesystem::exists(root / "core_sim/jsbsim/engine/flying_trainer_one_piston.xml"),
          "primary JSBSim piston engine model should exist");
  require(std::filesystem::exists(root / "core_sim/jsbsim/engine/flying_trainer_one_propeller.xml"),
          "primary JSBSim propeller model should exist");

  const FlightDynamicsInitialCondition propulsion_check_state =
      airborne_state_with_forward_speed(0.025, 25.0);
  const FlightDynamicsState stopped_state =
      run_aircraft(1.0, propulsion_check_state, stopped_controls());
  require_less_than(stopped_state.fuel_flow_kgps,
                    1.0e-5,
                    "JSBSim primary model should not burn fuel with the engine switch and magnetos off");

  AircraftControlInputSample no_starter = running_controls(0.50);
  no_starter.engine_starter_engaged = false;
  FlightDynamicsStepper no_starter_stepper{std::make_unique<JsbsimFlightDynamicsBackend>()};
  no_starter_stepper.reset(propulsion_check_state);
  advance_for(no_starter_stepper, 1.0, no_starter);
  require_less_than(no_starter_stepper.state().fuel_flow_kgps,
                    1.0e-5,
                    "JSBSim primary model should not start from run switch and magnetos without starter");

  AircraftControlInputSample starter_without_magnetos = running_controls(0.50);
  starter_without_magnetos.engine_starter_engaged = true;
  starter_without_magnetos.magnetos_on = false;
  FlightDynamicsStepper starter_without_magnetos_stepper{std::make_unique<JsbsimFlightDynamicsBackend>()};
  starter_without_magnetos_stepper.reset(propulsion_check_state);
  advance_for(starter_without_magnetos_stepper, 1.0, starter_without_magnetos);
  require_less_than(starter_without_magnetos_stepper.state().fuel_flow_kgps,
                    1.0e-5,
                    "JSBSim primary model should not start with starter engaged and magnetos off");

  AircraftControlInputSample starting = running_controls(0.35);
  starting.engine_starter_engaged = true;
  FlightDynamicsStepper lifecycle_stepper{std::make_unique<JsbsimFlightDynamicsBackend>()};
  lifecycle_stepper.reset(propulsion_check_state);
  advance_for(lifecycle_stepper, 1.0, starting);
  const FlightDynamicsState starting_state = lifecycle_stepper.state();
  require_greater_than(starting_state.engine_rpm,
                       stopped_state.engine_rpm + 50.0,
                       "JSBSim primary model should spin up with starter and magnetos commanded on");

  AircraftControlInputSample takeoff = running_controls(0.90);
  advance_for(lifecycle_stepper, 2.0, takeoff);
  const FlightDynamicsState takeoff_state = lifecycle_stepper.state();
  require_greater_than(takeoff_state.engine_rpm,
                       stopped_state.engine_rpm + 250.0,
                       "JSBSim primary model should increase engine RPM with throttle");
  require_greater_than(takeoff_state.propeller_thrust_n,
                       stopped_state.propeller_thrust_n + 3.0,
                       "JSBSim primary model should increase propeller thrust with throttle");
  require_greater_than(takeoff_state.fuel_flow_kgps,
                       stopped_state.fuel_flow_kgps + 0.001,
                       "JSBSim primary model should increase fuel burn with throttle");
  require(takeoff_state.fuel_quantity_kg < 143.0,
          "JSBSim primary model should consume fuel from the configured tank");

  const FlightDynamicsState running_state = lifecycle_stepper.state();
  advance_for(lifecycle_stepper, 2.0, stopped_controls());
  const FlightDynamicsState shutdown_state = lifecycle_stepper.state();
  require_less_than(shutdown_state.fuel_flow_kgps,
                    running_state.fuel_flow_kgps * 0.25,
                    "JSBSim primary model should stop fuel burn after explicit shutdown commands");

  FlightDynamicsStepper magneto_shutdown_stepper{std::make_unique<JsbsimFlightDynamicsBackend>()};
  magneto_shutdown_stepper.reset(propulsion_check_state);
  advance_for(magneto_shutdown_stepper, 1.0, starting);
  advance_for(magneto_shutdown_stepper, 2.0, takeoff);
  const FlightDynamicsState magneto_running_state = magneto_shutdown_stepper.state();
  AircraftControlInputSample magnetos_off = running_controls(0.20);
  magnetos_off.magnetos_on = false;
  advance_for(magneto_shutdown_stepper, 2.0, magnetos_off);
  const FlightDynamicsState magneto_shutdown_state = magneto_shutdown_stepper.state();
  require_less_than(magneto_shutdown_state.fuel_flow_kgps,
                    magneto_running_state.fuel_flow_kgps * 0.25,
                    "JSBSim primary model should stop fuel burn when magnetos are switched off");
}

void primary_aircraft_trims_required_airborne_states() {
  const TrimCandidate level = solve_trim(
      {"level", 0.0, 0.0, 0.0},
      {-0.15, -0.10, -0.05, 0.0},
      {0.65, 0.85, 1.0},
      {0.0},
      {0.10, 0.30},
      {0.0},
      {0.0});
  require_trim_equilibrium({"level", 0.0, 0.0, 0.0}, level, 3.0, 4.5, 0.45);

  const TrimCandidate climb = solve_trim(
      {"climb", -2.0, 0.0, 0.0},
      {-0.20, -0.15, -0.10, -0.05, 0.0, 0.05, 0.10},
      {1.0},
      {0.0},
      {0.10, 0.30},
      {0.0},
      {0.0});
  require_trim_equilibrium({"climb", -2.0, 0.0, 0.0}, climb, 3.0, 4.5, 0.45);
  require(climb.final.altitude_m > climb.one_second_before_final.altitude_m,
          "climb trim should gain altitude during the final sampled second");

  const TrimCandidate descent = solve_trim(
      {"descent", 8.0, 0.0, 0.0},
      {-0.12, -0.10},
      {0.20, 0.35},
      {0.05, 0.10, 0.15},
      {0.10, 0.20},
      {0.0},
      {0.0});
  require_trim_equilibrium({"descent", 8.0, 0.0, 0.0}, descent, 4.0, 4.5, 0.45);
  require(descent.final.altitude_m < descent.one_second_before_final.altitude_m,
          "descent trim should lose altitude during the final sampled second");

  constexpr double kTurnBankRad = 0.35;
  const TrimCandidate turn = solve_trim(
      {"coordinated turn", 0.0, kTurnBankRad, 0.045},
      {0.04},
      {0.68},
      {0.0},
      {-0.05},
      {0.08},
      {0.06},
      kTurnBankRad);
  require_trim_equilibrium({"coordinated turn", 0.0, kTurnBankRad, 0.045},
                           turn,
                           5.0,
                           5.0,
                           0.60);
  require(std::abs(turn.final.sideslip_rad) < 0.20,
          "coordinated turn trim should keep sideslip near coordinated flight");
  require_near(turn.final.angular_velocity_body_radps.z, 0.045, 0.08,
               "coordinated turn trim should maintain the configured yaw rate");
  require_near(turn.final.euler_rad.x, kTurnBankRad, 0.25,
               "coordinated turn trim should hold the configured bank angle");
}

void energy_momentum_gravity_atmosphere_and_units_are_consistent() {
  AircraftControlInputSample controls = running_controls(0.50);

  const FlightDynamicsState state = run_aircraft(2.0, airborne_state(0.025), controls);
  const double body_speed_mps = norm(state.body_velocity_mps);
  const double ecef_speed_mps = norm(state.ecef_velocity_mps);
  const double ned_speed_mps = norm(state.ned_velocity_mps);
  const double reference_specific_energy =
      kGravityMps2 * state.altitude_m + 0.5 * ecef_speed_mps * ecef_speed_mps;
  const double isa_temperature_k =
      kIsaSeaLevelTemperatureK - kIsaLapseRateKPerM * state.altitude_m;
  const double reference_speed_of_sound_mps =
      std::sqrt(kIsaGamma * kAirGasConstant * isa_temperature_k);
  const double mach_reference_speed_mps = state.mach * reference_speed_of_sound_mps;

  require_near(1'500.0 * kMetersToFeet, 4'921.259842519685, 1.0e-9,
               "meter-to-foot unit reference should match the NIST conversion");
  require_near(100.0 * kKnotsToMetersPerSecond, 51.44444444444445, 1.0e-12,
               "knot-to-meter-per-second unit reference should match the exact conversion");
  require_near(body_speed_mps, ecef_speed_mps, 1.0e-6,
               "body and ECEF momentum speed magnitudes should agree under rotation");
  require_near(body_speed_mps, ned_speed_mps, 1.0e-6,
               "body and NED momentum speed magnitudes should agree under rotation");
  require_near(specific_energy(state), reference_specific_energy, 1.0e-5,
               "specific energy should match independent SI energy calculation");
  require_near(mach_reference_speed_mps, body_speed_mps, 3.5,
               "Mach atmosphere reference should reconstruct true airspeed");
  require(state.altitude_m > state.terrain_elevation_m,
          "gravity and terrain references should preserve positive airborne clearance");

  AircraftControlInputSample idle = stopped_controls();
  require_near(kGravityMps2, 9.80665, 1.0e-12,
               "gravity reference should match the standard SI gravity constant");

  AircraftControlInputSample high_power = running_controls(0.95);
  const FlightDynamicsInitialCondition propulsion_reference_state =
      airborne_state_with_forward_speed(0.025, 25.0);
  const FlightSamplePair powered =
      run_aircraft_sample_pair(3.0, propulsion_reference_state, high_power);
  const FlightSamplePair unpowered =
      run_aircraft_sample_pair(3.0, propulsion_reference_state, idle);
  require_greater_than(powered.final.engine_rpm,
                       unpowered.final.engine_rpm + 250.0,
                       "propulsion reference should show higher RPM under high throttle");
  require_greater_than(powered.final.fuel_flow_kgps,
                       unpowered.final.fuel_flow_kgps + 0.001,
                       "fuel consumption reference should show higher fuel flow under high throttle");
}

void ground_contact_tests_cover_flat_sloped_and_rough_surfaces() {
  InMemoryTerrainHeightService terrain{make_geodetic_degrees(49.2, 16.6, {300.0})};
  terrain.add_dem_surface({
    TerrainLocalBounds{-100.0, -1.0, -100.0, 100.0},
    make_flat_terrain_plane(OrthometricHeight{320.0}),
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, true, 0.02, "flat"},
    "flat-tile",
    TerrainConfidenceMetadata{0.95, 0.2, 0.5, true},
    10,
    "flat-grass",
  });
  terrain.add_dem_surface({
    TerrainLocalBounds{0.0, 100.0, -100.0, -1.0},
    make_sloped_terrain_plane(OrthometricHeight{330.0}, 0.0, 0.0, 0.04, -0.02),
    TerrainSurfaceMaterial::kAsphalt,
    TerrainCollisionMetadata{true, true, 0.01, "sloped"},
    "sloped-tile",
    TerrainConfidenceMetadata{0.98, 0.1, 0.3, true},
    10,
    "sloped-asphalt",
  });
  terrain.add_dem_surface({
    TerrainLocalBounds{0.0, 100.0, 0.0, 100.0},
    make_sloped_terrain_plane(OrthometricHeight{318.0}, 0.0, 0.0, -0.07, 0.05),
    TerrainSurfaceMaterial::kGravel,
    TerrainCollisionMetadata{true, false, 0.08, "rough"},
    "rough-tile",
    TerrainConfidenceMetadata{0.80, 0.7, 1.4, true},
    10,
    "rough-gravel",
  });

  const auto flat_query = terrain.query_from_local_enu(EnuVector{-50.0, 0.0, 0.0});
  const auto [flat_latitude_deg, flat_longitude_deg] = degrees_from_local_query(flat_query);
  const auto flat = query_terrain_contact(
      terrain,
      TerrainContactQuery{flat_latitude_deg, flat_longitude_deg, 322.0});
  require_near(flat.clearance_m, 2.0, 1.0e-8, "flat surface clearance should be sampled");
  require_near(flat.surface_normal_ned.z, -1.0, 1.0e-12,
               "flat contact normal should point up in NED");

  const auto sloped_query = terrain.query_from_local_enu(EnuVector{50.0, -50.0, 0.0});
  const auto [sloped_latitude_deg, sloped_longitude_deg] = degrees_from_local_query(sloped_query);
  const auto sloped = query_terrain_contact(
      terrain,
      TerrainContactQuery{sloped_latitude_deg, sloped_longitude_deg, 336.0});
  require(sloped.surface_material == TerrainSurfaceMaterial::kAsphalt,
          "sloped surface material should be sampled");
  require(std::abs(sloped.surface_normal_ned.x) > 0.0 &&
              std::abs(sloped.surface_normal_ned.y) > 0.0,
          "sloped surface normal should include north/east components");

  const auto rough_query = terrain.query_from_local_enu(EnuVector{50.0, 50.0, 0.0});
  const auto [rough_latitude_deg, rough_longitude_deg] = degrees_from_local_query(rough_query);
  const auto rough = query_terrain_contact(
      terrain,
      TerrainContactQuery{rough_latitude_deg, rough_longitude_deg, 320.0});
  require(rough.surface_material == TerrainSurfaceMaterial::kGravel,
          "rough surface material should be sampled");
  require(!rough.collision_watertight && rough.collision_contact_offset_m > 0.0,
          "rough surface contact should retain collision roughness metadata");
}

} // namespace

int main() {
  primary_aircraft_package_is_present();
  primary_aircraft_trims_required_airborne_states();
  energy_momentum_gravity_atmosphere_and_units_are_consistent();
  ground_contact_tests_cover_flat_sloped_and_rough_surfaces();
  return 0;
}
