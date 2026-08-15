#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/jsbsim_adapter.hpp"
#include "flying/core_sim/units.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {

using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::FlightDynamicsInitialCondition;
using flying::core_sim::FlightDynamicsStepper;
using flying::core_sim::JsbsimFlightDynamicsBackend;
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

bool is_finite_vector(flying::core_sim::Vector3d value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

FlightDynamicsInitialCondition initial_condition() {
  FlightDynamicsInitialCondition initial{};
  initial.latitude_deg = 49.2;
  initial.longitude_deg = 16.6;
  initial.altitude_m = 1'500.0;
  initial.body_velocity_mps = {35.0, 0.0, 0.0};
  return initial;
}

FlightDynamicsStepper make_stepper() {
  FlightDynamicsStepper stepper{std::make_unique<JsbsimFlightDynamicsBackend>()};
  stepper.reset(initial_condition());
  return stepper;
}

void jsbsim_backend_records_identity_inputs_and_output_state() {
  auto stepper = make_stepper();
  const AircraftControlInputSample controls{0.10, -0.05, 0.02, 0.50, 0.0, 0.0, 0.0, 1.0};

  const auto report = stepper.advance(kFixedStepSeconds, controls);
  const auto& record = report.last_step_record;

  require(report.steps_executed == 1, "JSBSim should advance for one CoreSim fixed step");
  require(report.has_step_record, "JSBSim step should emit a replay record");
  require(report.step_records.size() == 1, "JSBSim report should retain the fixed-step record");
  require(record.aircraft.backend == "JSBSim", "record should include JSBSim backend identity");
  require(record.aircraft.model_name == "flying_trainer_one",
          "record should include primary aircraft identity");
  require_near(record.fixed_step_s, kFixedStepSeconds, 1.0e-15,
               "recorded JSBSim step should be the CoreSim 240 Hz interval");
  require_near(record.controls.throttle_norm, controls.throttle_norm, 0.0,
               "record should preserve throttle command");
  require_near(record.controls.aileron_norm, controls.aileron_norm, 0.0,
               "record should preserve aileron command");
  require(record.state.step_index == 1, "JSBSim state should advance exactly one fixed step");
  require(std::isfinite(record.state.simulation_time_s), "simulation time should be finite");
  require(std::isfinite(record.state.latitude_deg), "latitude should be finite");
  require(std::isfinite(record.state.longitude_deg), "longitude should be finite");
  require(std::isfinite(record.state.altitude_m), "altitude should be finite");
  require(is_finite_vector(record.state.ecef_position_m), "ECEF position should be finite");
  require(is_finite_vector(record.state.ecef_velocity_mps), "ECEF velocity should be finite");
  require(is_finite_vector(record.state.body_velocity_mps), "body velocity should be finite");
  require(record.record_hash == hash_flight_dynamics_step_record(record),
          "JSBSim record hash should cover stable aircraft identity, controls, and output state");
}

void jsbsim_primary_stream_is_repeatable() {
  auto run_primary_stream = [] {
    auto stepper = make_stepper();
    const std::array<std::pair<double, AircraftControlInputSample>, 8> stream{{
      {1.0 / 480.0, {0.05, -0.03, 0.01, 0.45, 0.0, 0.0, 0.0, 1.0}},
      {1.0 / 480.0, {0.05, -0.03, 0.01, 0.45, 0.0, 0.0, 0.0, 1.0}},
      {1.0 / 120.0, {0.02, -0.02, 0.00, 0.47, 0.0, 0.0, 0.0, 1.0}},
      {1.0 / 240.0, {-0.01, 0.01, -0.01, 0.43, 0.0, 0.0, 0.0, 1.0}},
      {1.0 / 720.0, {0.00, 0.00, 0.00, 0.42, 0.0, 0.0, 0.0, 1.0}},
      {1.0 / 720.0, {0.00, 0.00, 0.00, 0.42, 0.0, 0.0, 0.0, 1.0}},
      {1.0 / 720.0, {0.00, 0.00, 0.00, 0.42, 0.0, 0.0, 0.0, 1.0}},
      {1.0 / 60.0, {-0.02, 0.02, 0.03, 0.44, 0.0, 0.0, 0.0, 1.0}},
    }};

    for (const auto& sample : stream) {
      stepper.advance(sample.first, sample.second);
    }

    return stepper.last_step_record().record_hash;
  };

  const std::uint64_t first_hash = run_primary_stream();
  const std::uint64_t second_hash = run_primary_stream();
  const std::uint64_t third_hash = run_primary_stream();

  require(first_hash != 0, "primary JSBSim stream should produce a non-zero record hash");
  require(first_hash == second_hash, "first and second JSBSim primary runs should match");
  require(second_hash == third_hash, "second and third JSBSim primary runs should match");
}

} // namespace

int main() {
  jsbsim_backend_records_identity_inputs_and_output_state();
  jsbsim_primary_stream_is_repeatable();
  return 0;
}
