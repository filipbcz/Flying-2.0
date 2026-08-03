#include "flying/core_sim/simulator.hpp"

#include <cmath>
#include <stdexcept>

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

void validate_input(const ControlInputSample& input) {
  if (!is_finite(input.force_body_n) || !is_finite(input.moment_body_nm)) {
    throw std::invalid_argument("CoreSim force and moment inputs must be finite SI values");
  }
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

double CoreSimulator::fixed_step_s() const noexcept {
  return accumulator_.fixed_step_s();
}

void CoreSimulator::reset(AuthoritativeState state) noexcept {
  state_ = state;
  state_.body_to_ecef = state_.body_to_ecef.normalized();
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
  flight_dynamics_initial_condition_ = flight_dynamics_initial_condition;
  initial_aircraft_controls_ = initial_aircraft_controls;
  accumulator_.reset();
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
}

} // namespace flying::core_sim
