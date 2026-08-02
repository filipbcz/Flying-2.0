#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/units.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

using flying::core_sim::AircraftConfigurationIdentity;
using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::FlightDynamicsInitialCondition;
using flying::core_sim::FlightDynamicsState;
using flying::core_sim::FlightDynamicsStepRecord;
using flying::core_sim::FlightDynamicsStepper;
using flying::core_sim::IFlightDynamicsBackend;
using flying::core_sim::Vector3d;
using flying::core_sim::hash_flight_dynamics_state;
using flying::core_sim::hash_flight_dynamics_step_record;
using flying::core_sim::kFixedStepSeconds;

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

class RecordingBackend final : public IFlightDynamicsBackend {
public:
  const AircraftConfigurationIdentity& aircraft() const noexcept override {
    return aircraft_;
  }

  const FlightDynamicsState& state() const noexcept override {
    return state_;
  }

  const FlightDynamicsStepRecord& last_step_record() const noexcept override {
    return last_step_record_;
  }

  void reset(const FlightDynamicsInitialCondition& initial_condition) override {
    state_ = {};
    state_.latitude_deg = initial_condition.latitude_deg;
    state_.longitude_deg = initial_condition.longitude_deg;
    state_.altitude_m = initial_condition.altitude_m;
    state_.terrain_elevation_m = initial_condition.terrain_elevation_m;
    state_.body_velocity_mps = initial_condition.body_velocity_mps;
    state_.angular_velocity_body_radps = initial_condition.angular_velocity_body_radps;
    state_.euler_rad = {
      initial_condition.roll_rad,
      initial_condition.pitch_rad,
      initial_condition.heading_rad,
    };
    last_step_record_ = make_record({});
  }

  FlightDynamicsStepRecord step_fixed(double fixed_step_s,
                                      const AircraftControlInputSample& controls) override {
    require_near(fixed_step_s, kFixedStepSeconds, 1.0e-15,
                 "backend must receive the CoreSim 240 Hz step size");

    state_.simulation_time_s += fixed_step_s;
    ++state_.step_index;
    state_.body_velocity_mps.x += controls.throttle_norm * 0.125;
    state_.body_velocity_mps.y += controls.aileron_norm * 0.0625;
    state_.body_velocity_mps.z += controls.elevator_norm * 0.03125;
    state_.angular_velocity_body_radps.x += controls.aileron_norm * 0.002;
    state_.angular_velocity_body_radps.y += controls.elevator_norm * 0.002;
    state_.angular_velocity_body_radps.z += controls.rudder_norm * 0.002;
    state_.euler_rad.x += state_.angular_velocity_body_radps.x * fixed_step_s;
    state_.euler_rad.y += state_.angular_velocity_body_radps.y * fixed_step_s;
    state_.euler_rad.z += state_.angular_velocity_body_radps.z * fixed_step_s;
    state_.ecef_position_m += state_.body_velocity_mps * fixed_step_s;

    last_step_record_ = make_record(controls);
    return last_step_record_;
  }

private:
  [[nodiscard]] FlightDynamicsStepRecord make_record(
      const AircraftControlInputSample& controls) const noexcept {
    FlightDynamicsStepRecord record{};
    record.step_index = state_.step_index;
    record.fixed_step_s = kFixedStepSeconds;
    record.aircraft = aircraft_;
    record.controls = controls;
    record.state = state_;
    record.state_hash = hash_flight_dynamics_state(record.state);
    record.record_hash = hash_flight_dynamics_step_record(record);
    return record;
  }

  AircraftConfigurationIdentity aircraft_{
    "recording-test-backend",
    "placeholder_community_aircraft",
    "infrastructure-v1",
    "memory",
    "test-only",
  };
  FlightDynamicsState state_{};
  FlightDynamicsStepRecord last_step_record_{};
};

FlightDynamicsStepper make_stepper() {
  FlightDynamicsStepper stepper{std::make_unique<RecordingBackend>()};
  stepper.reset();
  return stepper;
}

std::uint64_t run_repeatable_stream() {
  auto stepper = make_stepper();
  const std::array<std::pair<double, AircraftControlInputSample>, 8> stream{{
    {1.0 / 480.0, {0.10, -0.05, 0.02, 0.55, 0.0, 0.0, 0.0, 1.0}},
    {1.0 / 480.0, {0.10, -0.05, 0.02, 0.55, 0.0, 0.0, 0.0, 1.0}},
    {1.0 / 120.0, {0.08, -0.04, 0.01, 0.60, 0.1, 0.0, 0.0, 1.0}},
    {1.0 / 240.0, {-0.05, 0.03, -0.02, 0.50, 0.1, 0.0, 0.0, 1.0}},
    {1.0 / 720.0, {0.02, 0.01, 0.00, 0.48, 0.0, 0.0, 0.0, 1.0}},
    {1.0 / 720.0, {0.02, 0.01, 0.00, 0.48, 0.0, 0.0, 0.0, 1.0}},
    {1.0 / 720.0, {0.02, 0.01, 0.00, 0.48, 0.0, 0.0, 0.0, 1.0}},
    {1.0 / 60.0, {-0.03, 0.02, 0.04, 0.52, 0.0, 0.0, 0.0, 1.0}},
  }};

  for (const auto& sample : stream) {
    stepper.advance(sample.first, sample.second);
  }

  return stepper.last_step_record().record_hash;
}

void stepper_batches_caller_frames_at_240_hz() {
  auto stepper = make_stepper();
  const AircraftControlInputSample controls{0.25, -0.10, 0.05, 0.75, 0.0, 0.0, 0.0, 1.0};

  const auto first_report = stepper.advance(1.0 / 480.0, controls);
  require(first_report.steps_executed == 0, "half-step caller frame should not step backend");
  require(!first_report.has_step_record, "sub-step report should not expose a new step record");
  require(first_report.step_records.empty(), "sub-step report should not include replay records");

  const auto second_report = stepper.advance(1.0 / 480.0, controls);
  require(second_report.steps_executed == 1, "two half-steps should execute one backend step");
  require(second_report.total_steps == 1, "total step count should be owned by CoreSim accumulator");
  require(second_report.has_step_record, "fixed-step report should expose the backend step record");
  require(second_report.step_records.size() == 1, "single fixed step should emit one replay record");
  require_near(second_report.fixed_step_s, kFixedStepSeconds, 1.0e-15,
               "fixed step should be the 240 Hz CoreSim cadence");
}

void stepper_reports_every_fixed_step_record() {
  auto stepper = make_stepper();
  const AircraftControlInputSample controls{0.05, 0.04, -0.02, 0.70, 0.0, 0.0, 0.0, 1.0};

  const auto report = stepper.advance(1.0 / 60.0, controls);

  require(report.steps_executed == 4, "1/60 second caller frame should emit four fixed steps");
  require(report.step_records.size() == 4, "report should retain every fixed-step record");
  require(report.last_step_record.step_index == 4, "last record should match the final emitted step");
  for (std::size_t index = 0; index < report.step_records.size(); ++index) {
    require(report.step_records[index].step_index == index + 1,
            "replay records should preserve monotonic step indexes");
    require_near(report.step_records[index].controls.throttle_norm, controls.throttle_norm, 0.0,
                 "each replay record should preserve the deterministic input sample");
  }
}

void step_records_capture_controls_aircraft_and_state() {
  auto stepper = make_stepper();
  const AircraftControlInputSample controls{-0.20, 0.15, -0.05, 0.65, 0.25, 0.1, 0.2, 0.9};

  const auto report = stepper.advance(kFixedStepSeconds, controls);

  require(report.has_step_record, "report should include the last step record");
  require(report.last_step_record.aircraft.backend == "recording-test-backend",
          "record should include backend identity");
  require(report.last_step_record.aircraft.model_name == "placeholder_community_aircraft",
          "record should include aircraft model identity");
  require_near(report.last_step_record.controls.throttle_norm, controls.throttle_norm, 0.0,
               "record should capture throttle input");
  require_near(report.last_step_record.controls.elevator_norm, controls.elevator_norm, 0.0,
               "record should capture elevator input");
  require(report.last_step_record.state.step_index == 1, "record should capture stepped state");
  require(report.last_step_record.state_hash == hash_flight_dynamics_state(report.last_step_record.state),
          "record state hash should match extracted state");
  require(report.last_step_record.record_hash == hash_flight_dynamics_step_record(report.last_step_record),
          "record hash should include controls, stable aircraft identity, and state");
}

void replay_hash_uses_stable_aircraft_identity() {
  FlightDynamicsStepRecord record{};
  record.step_index = 7;
  record.fixed_step_s = kFixedStepSeconds;
  record.aircraft = {
    "JSBSim",
    "flying_placeholder",
    "infrastructure-v1",
    "/workspace-a/core_sim/jsbsim",
    "Project-authored infrastructure placeholder; no fidelity claim",
  };
  record.controls = {0.10, -0.05, 0.02, 0.50, 0.0, 0.0, 0.0, 1.0};
  record.state.step_index = record.step_index;
  record.state.simulation_time_s = record.fixed_step_s * static_cast<double>(record.step_index);
  record.state.latitude_deg = 49.2;
  record.state.longitude_deg = 16.6;
  record.state.altitude_m = 1'499.5;
  record.state.body_velocity_mps = {35.2, -0.1, 0.05};
  record.state_hash = hash_flight_dynamics_state(record.state);

  auto relocated_record = record;
  relocated_record.aircraft.data_root = "/workspace-b/core_sim/jsbsim";

  auto changed_model_record = record;
  changed_model_record.aircraft.model_version = "infrastructure-v2";

  require(hash_flight_dynamics_step_record(record) ==
              hash_flight_dynamics_step_record(relocated_record),
          "replay hashes should not include machine-specific data_root metadata");
  require(hash_flight_dynamics_step_record(record) !=
              hash_flight_dynamics_step_record(changed_model_record),
          "replay hashes should include the stable aircraft model identity");
}

void repeated_streams_produce_identical_records() {
  const std::uint64_t first_hash = run_repeatable_stream();
  const std::uint64_t second_hash = run_repeatable_stream();
  const std::uint64_t third_hash = run_repeatable_stream();

  require(first_hash == second_hash, "first and second adapter streams should hash identically");
  require(second_hash == third_hash, "second and third adapter streams should hash identically");
}

void validation_rejects_non_deterministic_inputs() {
  auto stepper = make_stepper();
  bool threw = false;
  try {
    stepper.advance(kFixedStepSeconds, {0.0, 0.0, 0.0, 1.25, 0.0, 0.0, 0.0, 1.0});
  } catch (const std::invalid_argument&) {
    threw = true;
  }

  require(threw, "control inputs outside normalized ranges should be rejected");
}

} // namespace

int main() {
  stepper_batches_caller_frames_at_240_hz();
  stepper_reports_every_fixed_step_record();
  step_records_capture_controls_aircraft_and_state();
  replay_hash_uses_stable_aircraft_identity();
  repeated_streams_produce_identical_records();
  validation_rejects_non_deterministic_inputs();
  return 0;
}
