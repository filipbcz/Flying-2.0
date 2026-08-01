#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/fixed_step.hpp"
#include "flying/core_sim/simulator.hpp"
#include "flying/core_sim/state.hpp"
#include "flying/core_sim/units.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace {

using flying::core_sim::AuthoritativeState;
using flying::core_sim::CallerFrameInput;
using flying::core_sim::ControlInputSample;
using flying::core_sim::CoreSimulator;
using flying::core_sim::FixedStepAccumulator;
using flying::core_sim::RigidBodyParameters;
using flying::core_sim::Vector3d;
using flying::core_sim::hash_state;
using flying::core_sim::kFixedStepSeconds;

static_assert(std::is_same_v<decltype(AuthoritativeState{}.simulation_time_s), double>);
static_assert(std::is_same_v<decltype(AuthoritativeState{}.ecef_position_m.x), double>);
static_assert(std::is_same_v<decltype(AuthoritativeState{}.ecef_velocity_mps.y), double>);
static_assert(std::is_same_v<decltype(AuthoritativeState{}.body_to_ecef.z), double>);
static_assert(std::is_same_v<decltype(AuthoritativeState{}.angular_velocity_body_radps.x), double>);
static_assert(std::is_same_v<decltype(AuthoritativeState{}.accumulated_force_body_n.y), double>);
static_assert(std::is_same_v<decltype(AuthoritativeState{}.accumulated_moment_body_nm.z), double>);
static_assert(std::is_same_v<decltype(RigidBodyParameters{}.mass_kg), double>);
static_assert(std::is_same_v<decltype(RigidBodyParameters{}.inertia_diagonal_kg_m2.x), double>);

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

std::uint32_t consume_available_steps(FixedStepAccumulator& accumulator) {
  std::uint32_t steps = 0;
  while (accumulator.has_step()) {
    accumulator.consume_step();
    ++steps;
  }
  return steps;
}

AuthoritativeState initial_state() {
  AuthoritativeState state{};
  state.ecef_position_m = {3'980'000.0, 1'100'000.0, 4'870'000.0};
  state.ecef_velocity_mps = {32.0, 0.0, -0.2};
  return state;
}

std::uint64_t run_deterministic_stream() {
  CoreSimulator simulator{RigidBodyParameters{975.0, {875.0, 1'060.0, 1'420.0}}};
  simulator.reset(initial_state());

  const std::array<CallerFrameInput, 10> input_stream{{
    {1.0 / 480.0, ControlInputSample{{1'100.0, 0.0, 80.0}, {0.0, 18.0, 0.0}}},
    {1.0 / 480.0, ControlInputSample{{1'100.0, 0.0, 80.0}, {0.0, 18.0, 0.0}}},
    {1.0 / 120.0, ControlInputSample{{1'250.0, 10.0, 75.0}, {2.0, 16.0, -4.0}}},
    {1.0 / 60.0, ControlInputSample{{1'300.0, -12.0, 70.0}, {-3.0, 14.0, -6.0}}},
    {1.0 / 240.0, ControlInputSample{{1'420.0, -18.0, 65.0}, {-6.0, 12.0, -8.0}}},
    {0.001, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
    {0.002, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
    {1.0 / 240.0, ControlInputSample{{880.0, 5.0, 20.0}, {1.0, 4.0, 0.5}}},
    {1.0 / 360.0, ControlInputSample{{860.0, -5.0, 15.0}, {-1.0, 3.0, -0.5}}},
    {1.0 / 720.0, ControlInputSample{{860.0, -5.0, 15.0}, {-1.0, 3.0, -0.5}}},
  }};

  for (const auto& frame : input_stream) {
    simulator.advance(frame.elapsed_time_s, frame.controls);
  }

  return hash_state(simulator.state());
}

void accumulator_batches_variable_frame_time() {
  FixedStepAccumulator accumulator{};

  accumulator.add_elapsed_time(1.0 / 480.0);
  require(consume_available_steps(accumulator) == 0, "half step should not advance physics");
  require(accumulator.accumulated_time_s() > 0.0, "half step should remain accumulated");

  accumulator.add_elapsed_time(1.0 / 480.0);
  require(consume_available_steps(accumulator) == 1, "two half steps should advance one fixed step");
  require_near(accumulator.accumulated_time_s(), 0.0, 1.0e-15, "accumulator should drain after one step");

  accumulator.add_elapsed_time(1.0 / 120.0);
  require(consume_available_steps(accumulator) == 2, "1/120 second should advance two fixed steps");

  accumulator.add_elapsed_time(1.0 / 60.0);
  require(consume_available_steps(accumulator) == 4, "1/60 second should advance four fixed steps");

  accumulator.add_elapsed_time(1.0 / 960.0);
  require(consume_available_steps(accumulator) == 0, "quarter step should remain accumulated");
  accumulator.add_elapsed_time(3.0 / 960.0);
  require(consume_available_steps(accumulator) == 1, "accumulated quarters should advance one step");
  require(accumulator.total_steps() == 8, "total fixed step count should include all consumed steps");
}

void simulator_uses_fixed_step_scheduler() {
  CoreSimulator simulator{RigidBodyParameters{975.0, {875.0, 1'060.0, 1'420.0}}};
  simulator.reset(initial_state());

  const ControlInputSample input{{1'200.0, 0.0, 50.0}, {0.0, 15.0, -2.0}};
  const auto first_report = simulator.advance(1.0 / 480.0, input);
  require(first_report.steps_executed == 0, "sub-step caller frame should not integrate");
  require(simulator.state().step_index == 0, "state should remain at step zero before accumulation completes");

  const auto second_report = simulator.advance(1.0 / 480.0, input);
  require(second_report.steps_executed == 1, "second half frame should produce one fixed step");
  require(simulator.state().step_index == 1, "state should advance by exactly one fixed step");
  require_near(simulator.state().simulation_time_s, kFixedStepSeconds, 1.0e-15, "simulation time must be fixed step duration");

  const auto third_report = simulator.advance(1.0 / 60.0, input);
  require(third_report.steps_executed == 4, "1/60 second caller frame should produce four fixed steps");
  require(third_report.total_steps == 5, "reported total steps should match scheduler consumption");
  require(simulator.state().step_index == 5, "state step index should match fixed step count");
}

void simulator_integrates_synthetic_rigid_body_state() {
  CoreSimulator simulator{RigidBodyParameters{1'000.0, {500.0, 600.0, 700.0}}};
  simulator.reset(initial_state());

  const Vector3d initial_position = simulator.state().ecef_position_m;
  const auto report = simulator.advance(kFixedStepSeconds, {{1'000.0, 0.0, 0.0}, {0.0, 60.0, 0.0}});

  require(report.steps_executed == 1, "exact fixed step should integrate once");
  require(simulator.state().ecef_position_m.x > initial_position.x, "positive body-x force should move ECEF x for identity attitude");
  require(simulator.state().angular_velocity_body_radps.y > 0.0, "positive pitch moment should update angular velocity");
  const double attitude_norm =
      std::sqrt(simulator.state().body_to_ecef.w * simulator.state().body_to_ecef.w +
                simulator.state().body_to_ecef.x * simulator.state().body_to_ecef.x +
                simulator.state().body_to_ecef.y * simulator.state().body_to_ecef.y +
                simulator.state().body_to_ecef.z * simulator.state().body_to_ecef.z);
  require_near(attitude_norm, 1.0, 1.0e-12, "orientation should remain normalized after integration");
}

void repeated_runs_produce_identical_state_hashes() {
  const std::uint64_t first_hash = run_deterministic_stream();
  const std::uint64_t second_hash = run_deterministic_stream();
  const std::uint64_t third_hash = run_deterministic_stream();

  require(first_hash == second_hash, "first and second deterministic runs should hash identically");
  require(second_hash == third_hash, "second and third deterministic runs should hash identically");
}

} // namespace

int main() {
  accumulator_batches_variable_frame_time();
  simulator_uses_fixed_step_scheduler();
  simulator_integrates_synthetic_rigid_body_state();
  repeated_runs_produce_identical_state_hashes();
  return 0;
}
