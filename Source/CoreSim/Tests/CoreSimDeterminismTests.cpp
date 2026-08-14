#include "CoreSim.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {

using flying::core_sim::AuthoritativeState;
using flying::core_sim::CallerFrameInput;
using flying::core_sim::ControlInputSample;
using flying::core_sim::CoreSimulator;
using flying::core_sim::FixedStepAccumulator;
using flying::core_sim::RigidBodyParameters;
using flying::core_sim::hash_state;
using flying::core_sim::kFixedStepFrequencyHz;
using flying::core_sim::kFixedStepSeconds;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

AuthoritativeState make_initial_state() {
  AuthoritativeState state{};
  state.ecef_position_m = {3'980'000.0, 1'100'000.0, 4'870'000.0};
  state.ecef_velocity_mps = {32.0, 0.0, -0.2};
  return state;
}

ControlInputSample make_controls(std::uint64_t step_index) {
  const double phase = static_cast<double>(step_index % 17u);
  return {
      {1'050.0 + phase * 12.0, -20.0 + phase, 35.0 + phase * 0.5},
      {-3.0 + phase * 0.25, 8.0 + phase * 0.75, -4.0 + phase * 0.5},
  };
}

std::vector<std::uint64_t> run_fixed_step_sequence(std::uint64_t step_count) {
  CoreSimulator simulator{RigidBodyParameters{975.0, {875.0, 1'060.0, 1'420.0}}};
  simulator.reset(make_initial_state());

  std::vector<std::uint64_t> hashes;
  hashes.reserve(static_cast<std::size_t>(step_count));
  for (std::uint64_t index = 0; index < step_count; ++index) {
    const auto report = simulator.advance(kFixedStepSeconds, make_controls(index));
    require(report.steps_executed == 1, "exact fixed-step input must execute one step");
    hashes.push_back(hash_state(simulator.state()));
  }
  return hashes;
}

std::vector<std::uint64_t> run_accumulated_step_sequence(
    const std::vector<CallerFrameInput>& frames) {
  CoreSimulator simulator{RigidBodyParameters{975.0, {875.0, 1'060.0, 1'420.0}}};
  simulator.reset(make_initial_state());

  FixedStepAccumulator accumulator{};
  std::vector<std::uint64_t> hashes;
  for (const auto& frame : frames) {
    accumulator.add_elapsed_time(frame.elapsed_time_s);
    while (accumulator.has_step()) {
      simulator.integrate_fixed_step(frame.controls);
      accumulator.consume_step();
      hashes.push_back(hash_state(simulator.state()));
    }
  }
  return hashes;
}

void fixed_step_clock_is_240_hz() {
  static_assert(kFixedStepFrequencyHz == 240.0);
  require(std::abs(kFixedStepSeconds - (1.0 / 240.0)) < 1.0e-18,
          "CoreSim fixed-step duration must be exactly 1/240 second");
}

void identical_inputs_produce_identical_state_hashes() {
  const auto first = run_fixed_step_sequence(96);
  const auto second = run_fixed_step_sequence(96);

  require(first.size() == second.size(), "deterministic runs must produce equal hash counts");
  for (std::size_t index = 0; index < first.size(); ++index) {
    require(first[index] == second[index], "identical fixed-step inputs must produce identical hashes");
  }
}

void render_frame_jitter_does_not_change_physical_state() {
  const ControlInputSample stable_input = make_controls(0);
  const std::vector<CallerFrameInput> steady_frames{
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 240.0, stable_input},
  };
  const std::vector<CallerFrameInput> jittered_frames{
      {1.0 / 480.0, stable_input},
      {1.0 / 480.0, stable_input},
      {1.0 / 120.0, stable_input},
      {1.0 / 240.0, stable_input},
      {1.0 / 80.0, stable_input},
      {0.001, stable_input},
      {0.002, stable_input},
      {(1.0 / 15.0) - (1.0 / 480.0 + 1.0 / 480.0 + 1.0 / 120.0 +
                       1.0 / 240.0 + 1.0 / 80.0 + 0.001 + 0.002),
       stable_input},
  };

  const auto steady_hashes = run_accumulated_step_sequence(steady_frames);
  const auto jittered_hashes = run_accumulated_step_sequence(jittered_frames);
  require(steady_hashes.size() == 16, "steady caller frames must emit 16 physical states");
  require(jittered_hashes.size() == 16, "jittered caller frames must emit 16 physical states");
  require(steady_hashes == jittered_hashes,
          "caller render-frame jitter must not alter the per-step physical state sequence");
}

} // namespace

int main() {
  fixed_step_clock_is_240_hz();
  identical_inputs_produce_identical_state_hashes();
  render_frame_jitter_does_not_change_physical_state();
  return 0;
}
