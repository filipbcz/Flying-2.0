#include "flying/core_sim/simulator.hpp"

#include "flying/geo_terrain/geodesy.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace flying::core_sim {
namespace {

[[nodiscard]] bool is_finite(Vector3d value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validate_parameters(const RigidBodyParameters& parameters) {
  if (parameters.mass_kg <= 0.0 || !std::isfinite(parameters.mass_kg)) {
    throw std::invalid_argument("mass_kg must be a positive finite SI mass");
  }
  if (parameters.inertia_diagonal_kg_m2.x <= 0.0 ||
      parameters.inertia_diagonal_kg_m2.y <= 0.0 ||
      parameters.inertia_diagonal_kg_m2.z <= 0.0 ||
      !is_finite(parameters.inertia_diagonal_kg_m2)) {
    throw std::invalid_argument("inertia_diagonal_kg_m2 must contain positive finite SI inertia values");
  }
}

void validate_mass_balance(const AircraftMassBalanceState& mass_balance) {
  if (mass_balance.total_mass_kg <= 0.0 || !std::isfinite(mass_balance.total_mass_kg)) {
    throw std::invalid_argument("aircraft mass balance total_mass_kg must be a positive finite SI mass");
  }
  if (mass_balance.fuel_mass_kg < 0.0 ||
      mass_balance.payload_mass_kg < 0.0 ||
      !std::isfinite(mass_balance.fuel_mass_kg) ||
      !std::isfinite(mass_balance.payload_mass_kg)) {
    throw std::invalid_argument("aircraft fuel and payload masses must be non-negative finite SI values");
  }
  if (!is_finite(mass_balance.center_of_gravity_body_m)) {
    throw std::invalid_argument("aircraft center_of_gravity_body_m must contain finite SI values");
  }
  const AircraftInertiaTensor& inertia = mass_balance.inertia_tensor_kg_m2;
  if (inertia.ixx <= 0.0 ||
      inertia.iyy <= 0.0 ||
      inertia.izz <= 0.0 ||
      !std::isfinite(inertia.ixx) ||
      !std::isfinite(inertia.iyy) ||
      !std::isfinite(inertia.izz) ||
      !std::isfinite(inertia.ixy) ||
      !std::isfinite(inertia.ixz) ||
      !std::isfinite(inertia.iyz)) {
    throw std::invalid_argument("aircraft inertia_tensor_kg_m2 must contain finite values and positive diagonal moments");
  }
  if (!mass_balance.cg_within_envelope) {
    throw std::invalid_argument("aircraft center of gravity is outside the configured envelope");
  }
}

[[nodiscard]] AircraftMassBalanceState mass_balance_from_parameters(
    const RigidBodyParameters& parameters) noexcept {
  AircraftMassBalanceState mass_balance{};
  mass_balance.total_mass_kg = parameters.mass_kg;
  mass_balance.inertia_tensor_kg_m2 = {
    parameters.inertia_diagonal_kg_m2.x,
    parameters.inertia_diagonal_kg_m2.y,
    parameters.inertia_diagonal_kg_m2.z,
    0.0,
    0.0,
    0.0,
  };
  mass_balance.cg_within_envelope = true;
  return mass_balance;
}

void apply_mass_balance_to_parameters(AircraftMassBalanceState mass_balance,
                                      RigidBodyParameters& parameters) noexcept {
  parameters.mass_kg = mass_balance.total_mass_kg;
  parameters.inertia_diagonal_kg_m2 = {
    mass_balance.inertia_tensor_kg_m2.ixx,
    mass_balance.inertia_tensor_kg_m2.iyy,
    mass_balance.inertia_tensor_kg_m2.izz,
  };
}

void validate_input(const ControlInputSample& input) {
  if (!is_finite(input.force_body_n) || !is_finite(input.moment_body_nm)) {
    throw std::invalid_argument("CoreSim force and moment inputs must be finite SI values");
  }
}

[[nodiscard]] geo_terrain::GeodeticCoordinates geodetic_from_state(
    const AuthoritativeState& state) noexcept {
  if (!is_finite(state.ecef_position_m) || dot(state.ecef_position_m, state.ecef_position_m) <= 0.0) {
    return geo_terrain::make_geodetic_degrees(
        49.2,
        16.6,
        geo_terrain::EllipsoidalHeight{0.0});
  }
  return geo_terrain::ecef_to_geodetic(
      geo_terrain::EcefPosition{{state.ecef_position_m.x, state.ecef_position_m.y, state.ecef_position_m.z}});
}

[[nodiscard]] Vector3d ecef_from_ned(const geo_terrain::LocalTangentFrame& frame,
                                     Vector3d ned_mps) noexcept {
  const geo_terrain::EcefVector ecef = geo_terrain::ecef_vector_from_ned(
      frame,
      geo_terrain::NedVector{ned_mps.x, ned_mps.y, ned_mps.z});
  return {ecef.meters.x, ecef.meters.y, ecef.meters.z};
}

void update_weather_state(AuthoritativeState& state, const WeatherModel& weather_model) noexcept {
  const geo_terrain::GeodeticCoordinates geodetic = geodetic_from_state(state);
  state.weather = weather_model.sample(geodetic.latitude_degrees(),
                                       geodetic.longitude_degrees(),
                                       geodetic.ellipsoidal_height.meters,
                                       state.simulation_time_s);
  const geo_terrain::LocalTangentFrame frame = geo_terrain::make_local_tangent_frame(geodetic);
  const Vector3d wind_ecef_mps = ecef_from_ned(frame, state.weather.wind_ned_mps);
  const Vector3d relative_air_ecef_mps = state.ecef_velocity_mps - wind_ecef_mps;
  state.relative_air_velocity_body_mps =
      state.body_to_ecef.conjugated().rotate(relative_air_ecef_mps);
  state.weather_dynamic_pressure_pa =
      0.5 * std::max(0.0, state.weather.atmosphere.density_kgpm3) *
      dot(state.relative_air_velocity_body_mps, state.relative_air_velocity_body_mps);
}

[[nodiscard]] Quaterniond integrate_orientation(
    Quaterniond body_to_ecef,
    Vector3d angular_velocity_body_radps,
    double fixed_step_s) noexcept {
  const Quaterniond omega_body{
    0.0,
    angular_velocity_body_radps.x,
    angular_velocity_body_radps.y,
    angular_velocity_body_radps.z,
  };
  const Quaterniond derivative = body_to_ecef * omega_body;

  body_to_ecef.w += 0.5 * derivative.w * fixed_step_s;
  body_to_ecef.x += 0.5 * derivative.x * fixed_step_s;
  body_to_ecef.y += 0.5 * derivative.y * fixed_step_s;
  body_to_ecef.z += 0.5 * derivative.z * fixed_step_s;
  return body_to_ecef.normalized();
}

} // namespace

CoreSimulator::CoreSimulator(RigidBodyParameters parameters)
    : parameters_(parameters) {
  validate_parameters(parameters_);
  reset();
}

const AuthoritativeState& CoreSimulator::state() const noexcept {
  return state_;
}

const FlightDynamicsInitialCondition& CoreSimulator::flight_dynamics_initial_condition()
    const noexcept {
  return flight_dynamics_initial_condition_;
}

const AircraftControlInputSample& CoreSimulator::initial_aircraft_controls() const noexcept {
  return initial_aircraft_controls_;
}

const RigidBodyParameters& CoreSimulator::parameters() const noexcept {
  return parameters_;
}

const AircraftMassBalanceState& CoreSimulator::aircraft_mass_balance() const noexcept {
  return state_.aircraft_mass_balance;
}

const WeatherSample& CoreSimulator::weather() const noexcept {
  return state_.weather;
}

const WeatherScenario& CoreSimulator::weather_scenario() const noexcept {
  return weather_model_.scenario();
}

double CoreSimulator::fixed_step_s() const noexcept {
  return accumulator_.fixed_step_s();
}

void CoreSimulator::reset(AuthoritativeState state) noexcept {
  state_ = state;
  state_.body_to_ecef = state_.body_to_ecef.normalized();
  state_.aircraft_mass_balance = mass_balance_from_parameters(parameters_);
  update_weather_state(state_, weather_model_);
  flight_dynamics_initial_condition_ = {};
  initial_aircraft_controls_ = {};
  accumulator_.reset();
}

void CoreSimulator::reset(AuthoritativeState state,
                          const FlightDynamicsInitialCondition& flight_dynamics_initial_condition,
                          const AircraftControlInputSample& initial_aircraft_controls) {
  validate_flight_dynamics_initial_condition(flight_dynamics_initial_condition);
  validate_aircraft_controls(initial_aircraft_controls);
  state_ = state;
  state_.body_to_ecef = state_.body_to_ecef.normalized();
  state_.aircraft_mass_balance = mass_balance_from_parameters(parameters_);
  update_weather_state(state_, weather_model_);
  flight_dynamics_initial_condition_ = flight_dynamics_initial_condition;
  initial_aircraft_controls_ = initial_aircraft_controls;
  accumulator_.reset();
}

void CoreSimulator::set_aircraft_mass_balance(const AircraftMassBalanceState& mass_balance) {
  validate_mass_balance(mass_balance);
  state_.aircraft_mass_balance = mass_balance;
  apply_mass_balance_to_parameters(mass_balance, parameters_);
}

void CoreSimulator::set_manual_weather_scenario(WeatherScenario scenario) {
  weather_model_.set_manual_scenario(std::move(scenario));
  update_weather_state(state_, weather_model_);
}

AdvanceReport CoreSimulator::advance(double caller_delta_s, const ControlInputSample& input) {
  validate_input(input);
  accumulator_.add_elapsed_time(caller_delta_s);

  std::uint32_t steps_executed = 0;
  while (accumulator_.has_step()) {
    integrate_fixed_step(input);
    accumulator_.consume_step();
    ++steps_executed;
  }

  return {
    steps_executed,
    accumulator_.fixed_step_s(),
    static_cast<double>(steps_executed) * accumulator_.fixed_step_s(),
    accumulator_.accumulated_time_s(),
    accumulator_.total_steps(),
    hash_state(state_),
  };
}

void CoreSimulator::integrate_fixed_step(const ControlInputSample& input) {
  validate_input(input);

  const double fixed_step_s = accumulator_.fixed_step_s();
  const Vector3d force_ecef_n = state_.body_to_ecef.rotate(input.force_body_n);
  const Vector3d acceleration_ecef_mps2 = force_ecef_n / parameters_.mass_kg;
  state_.ecef_velocity_mps += acceleration_ecef_mps2 * fixed_step_s;
  state_.ecef_position_m += state_.ecef_velocity_mps * fixed_step_s;

  const Vector3d angular_acceleration_body_radps2{
    input.moment_body_nm.x / parameters_.inertia_diagonal_kg_m2.x,
    input.moment_body_nm.y / parameters_.inertia_diagonal_kg_m2.y,
    input.moment_body_nm.z / parameters_.inertia_diagonal_kg_m2.z,
  };
  state_.angular_velocity_body_radps += angular_acceleration_body_radps2 * fixed_step_s;
  state_.body_to_ecef =
      integrate_orientation(state_.body_to_ecef, state_.angular_velocity_body_radps, fixed_step_s);

  state_.accumulated_force_body_n = input.force_body_n;
  state_.accumulated_moment_body_nm = input.moment_body_nm;
  state_.simulation_time_s += fixed_step_s;
  ++state_.step_index;
  update_weather_state(state_, weather_model_);
}

} // namespace flying::core_sim
