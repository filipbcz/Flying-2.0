#include "flying/core_sim/flight_dynamics.hpp"

#include <bit>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace flying::core_sim {
namespace {

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ull;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ull;
constexpr std::uint64_t kFlightDynamicsStateHashSchemaVersion = 1;
constexpr std::uint64_t kFlightDynamicsRecordHashSchemaVersion = 1;

[[nodiscard]] bool is_finite(Vector3d value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void require_finite(double value, const char* field_name) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(std::string(field_name) + " must be finite");
  }
}

void require_signed_unit(double value, const char* field_name) {
  require_finite(value, field_name);
  if (value < -1.0 || value > 1.0) {
    throw std::invalid_argument(std::string(field_name) + " must be in the normalized [-1, 1] range");
  }
}

void require_unit_interval(double value, const char* field_name) {
  require_finite(value, field_name);
  if (value < 0.0 || value > 1.0) {
    throw std::invalid_argument(std::string(field_name) + " must be in the normalized [0, 1] range");
  }
}

[[nodiscard]] std::uint64_t append_byte(std::uint64_t hash, std::uint8_t value) noexcept {
  hash ^= value;
  hash *= kFnvPrime;
  return hash;
}

[[nodiscard]] std::uint64_t append_u64(std::uint64_t hash, std::uint64_t value) noexcept {
  for (int byte_index = 0; byte_index < 8; ++byte_index) {
    hash = append_byte(hash, static_cast<std::uint8_t>((value >> (byte_index * 8)) & 0xffu));
  }
  return hash;
}

[[nodiscard]] std::uint64_t canonical_double_bits(double value) noexcept {
  if (value == 0.0) {
    value = 0.0;
  }
  if (std::isnan(value)) {
    return 0x7ff8'0000'0000'0000ull;
  }
  return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint64_t append_double(std::uint64_t hash, double value) noexcept {
  return append_u64(hash, canonical_double_bits(value));
}

[[nodiscard]] std::uint64_t append_vector(std::uint64_t hash, Vector3d value) noexcept {
  hash = append_double(hash, value.x);
  hash = append_double(hash, value.y);
  return append_double(hash, value.z);
}

[[nodiscard]] std::uint64_t append_quaternion(std::uint64_t hash, Quaterniond value) noexcept {
  hash = append_double(hash, value.w);
  hash = append_double(hash, value.x);
  hash = append_double(hash, value.y);
  return append_double(hash, value.z);
}

[[nodiscard]] std::uint64_t append_string(std::uint64_t hash, std::string_view value) noexcept {
  hash = append_u64(hash, value.size());
  for (unsigned char byte : value) {
    hash = append_byte(hash, byte);
  }
  return hash;
}

[[nodiscard]] std::uint64_t append_controls(std::uint64_t hash,
                                            const AircraftControlInputSample& controls) noexcept {
  hash = append_double(hash, controls.aileron_norm);
  hash = append_double(hash, controls.elevator_norm);
  hash = append_double(hash, controls.rudder_norm);
  hash = append_double(hash, controls.throttle_norm);
  hash = append_double(hash, controls.flaps_norm);
  hash = append_double(hash, controls.brake_left_norm);
  hash = append_double(hash, controls.brake_right_norm);
  hash = append_double(hash, controls.mixture_norm);
  hash = append_double(hash, controls.propeller_norm);
  hash = append_double(hash, controls.elevator_trim_norm);
  hash = append_double(hash, controls.aileron_trim_norm);
  return append_double(hash, controls.rudder_trim_norm);
}

[[nodiscard]] std::uint64_t append_stable_aircraft_identity(
    std::uint64_t hash,
    const AircraftConfigurationIdentity& aircraft) noexcept {
  hash = append_string(hash, aircraft.backend);
  hash = append_string(hash, aircraft.model_name);
  hash = append_string(hash, aircraft.model_version);
  return append_string(hash, aircraft.source_license);
}

} // namespace

FlightDynamicsStepper::FlightDynamicsStepper(std::unique_ptr<IFlightDynamicsBackend> backend,
                                             double fixed_step_s)
    : accumulator_(fixed_step_s),
      backend_(std::move(backend)) {
  if (!backend_) {
    throw std::invalid_argument("FlightDynamicsStepper requires a backend");
  }
}

const AircraftConfigurationIdentity& FlightDynamicsStepper::aircraft() const noexcept {
  return backend_->aircraft();
}

const FlightDynamicsState& FlightDynamicsStepper::state() const noexcept {
  return backend_->state();
}

const FlightDynamicsStepRecord& FlightDynamicsStepper::last_step_record() const noexcept {
  return backend_->last_step_record();
}

double FlightDynamicsStepper::fixed_step_s() const noexcept {
  return accumulator_.fixed_step_s();
}

void FlightDynamicsStepper::reset(const FlightDynamicsInitialCondition& initial_condition) {
  validate_flight_dynamics_initial_condition(initial_condition);
  backend_->reset(initial_condition);
  accumulator_.reset();
}

FlightDynamicsAdvanceReport FlightDynamicsStepper::advance(double caller_delta_s,
                                                           const AircraftControlInputSample& controls) {
  validate_aircraft_controls(controls);
  accumulator_.add_elapsed_time(caller_delta_s);

  std::uint32_t steps_executed = 0;
  std::vector<FlightDynamicsStepRecord> step_records;
  while (accumulator_.has_step()) {
    step_records.push_back(backend_->step_fixed(accumulator_.fixed_step_s(), controls));
    accumulator_.consume_step();
    ++steps_executed;
  }

  const bool stepped_this_advance = !step_records.empty();

  return {
    steps_executed,
    accumulator_.fixed_step_s(),
    static_cast<double>(steps_executed) * accumulator_.fixed_step_s(),
    accumulator_.accumulated_time_s(),
    accumulator_.total_steps(),
    stepped_this_advance,
    stepped_this_advance ? backend_->last_step_record() : FlightDynamicsStepRecord{},
    std::move(step_records),
  };
}

void validate_flight_dynamics_initial_condition(
    const FlightDynamicsInitialCondition& initial_condition) {
  require_finite(initial_condition.latitude_deg, "latitude_deg");
  require_finite(initial_condition.longitude_deg, "longitude_deg");
  require_finite(initial_condition.altitude_m, "altitude_m");
  require_finite(initial_condition.terrain_elevation_m, "terrain_elevation_m");

  if (initial_condition.latitude_deg < -90.0 || initial_condition.latitude_deg > 90.0) {
    throw std::invalid_argument("latitude_deg must be in the [-90, 90] range");
  }
  if (initial_condition.longitude_deg < -180.0 || initial_condition.longitude_deg > 180.0) {
    throw std::invalid_argument("longitude_deg must be in the [-180, 180] range");
  }
  if (!is_finite(initial_condition.body_velocity_mps) ||
      !is_finite(initial_condition.angular_velocity_body_radps)) {
    throw std::invalid_argument("initial velocity vectors must contain finite SI values");
  }

  require_finite(initial_condition.roll_rad, "roll_rad");
  require_finite(initial_condition.pitch_rad, "pitch_rad");
  require_finite(initial_condition.heading_rad, "heading_rad");
}

void validate_aircraft_controls(const AircraftControlInputSample& controls) {
  require_signed_unit(controls.aileron_norm, "aileron_norm");
  require_signed_unit(controls.elevator_norm, "elevator_norm");
  require_signed_unit(controls.rudder_norm, "rudder_norm");
  require_unit_interval(controls.throttle_norm, "throttle_norm");
  require_unit_interval(controls.flaps_norm, "flaps_norm");
  require_unit_interval(controls.brake_left_norm, "brake_left_norm");
  require_unit_interval(controls.brake_right_norm, "brake_right_norm");
  require_unit_interval(controls.mixture_norm, "mixture_norm");
  require_unit_interval(controls.propeller_norm, "propeller_norm");
  require_signed_unit(controls.elevator_trim_norm, "elevator_trim_norm");
  require_signed_unit(controls.aileron_trim_norm, "aileron_trim_norm");
  require_signed_unit(controls.rudder_trim_norm, "rudder_trim_norm");
}

std::uint64_t hash_flight_dynamics_state(const FlightDynamicsState& state) noexcept {
  auto hash = append_u64(kFnvOffset, kFlightDynamicsStateHashSchemaVersion);
  hash = append_double(hash, state.simulation_time_s);
  hash = append_u64(hash, state.step_index);
  hash = append_double(hash, state.latitude_deg);
  hash = append_double(hash, state.longitude_deg);
  hash = append_double(hash, state.altitude_m);
  hash = append_double(hash, state.terrain_elevation_m);
  hash = append_vector(hash, state.ecef_position_m);
  hash = append_vector(hash, state.ecef_velocity_mps);
  hash = append_vector(hash, state.ned_velocity_mps);
  hash = append_vector(hash, state.body_velocity_mps);
  hash = append_quaternion(hash, state.body_to_ecef);
  hash = append_vector(hash, state.euler_rad);
  hash = append_vector(hash, state.angular_velocity_body_radps);
  hash = append_vector(hash, state.total_force_body_n);
  hash = append_vector(hash, state.total_moment_body_nm);
  hash = append_double(hash, state.angle_of_attack_rad);
  hash = append_double(hash, state.sideslip_rad);
  hash = append_double(hash, state.mach);
  return append_double(hash, state.calibrated_airspeed_mps);
}

std::uint64_t hash_flight_dynamics_step_record(const FlightDynamicsStepRecord& record) noexcept {
  auto hash = append_u64(kFnvOffset, kFlightDynamicsRecordHashSchemaVersion);
  hash = append_u64(hash, record.step_index);
  hash = append_double(hash, record.fixed_step_s);
  hash = append_stable_aircraft_identity(hash, record.aircraft);
  hash = append_controls(hash, record.controls);
  hash = append_u64(hash, record.state_hash);
  return append_u64(hash, hash_flight_dynamics_state(record.state));
}

} // namespace flying::core_sim
