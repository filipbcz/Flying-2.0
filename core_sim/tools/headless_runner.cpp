#include "flying/core_sim/simulator.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>

int main() {
  using flying::core_sim::AuthoritativeState;
  using flying::core_sim::CallerFrameInput;
  using flying::core_sim::ControlInputSample;
  using flying::core_sim::CoreSimulator;
  using flying::core_sim::RigidBodyParameters;

  const RigidBodyParameters parameters{
    975.0,
    {875.0, 1'060.0, 1'420.0},
  };

  AuthoritativeState initial_state{};
  initial_state.ecef_position_m = {3'980'000.0, 1'100'000.0, 4'870'000.0};
  initial_state.ecef_velocity_mps = {32.0, 0.0, -0.2};

  CoreSimulator simulator{parameters};
  simulator.reset(initial_state);

  const std::array<CallerFrameInput, 8> input_stream{{
    {1.0 / 480.0, ControlInputSample{{1'100.0, 0.0, 80.0}, {0.0, 18.0, 0.0}}},
    {1.0 / 480.0, ControlInputSample{{1'100.0, 0.0, 80.0}, {0.0, 18.0, 0.0}}},
    {1.0 / 120.0, ControlInputSample{{1'250.0, 10.0, 75.0}, {2.0, 16.0, -4.0}}},
    {1.0 / 240.0, ControlInputSample{{1'300.0, -12.0, 70.0}, {-3.0, 14.0, -6.0}}},
    {1.0 / 80.0, ControlInputSample{{1'420.0, -18.0, 65.0}, {-6.0, 12.0, -8.0}}},
    {0.001, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
    {0.004, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
    {0.002, ControlInputSample{{900.0, 0.0, 25.0}, {0.0, 5.0, 0.0}}},
  }};

  std::cout << std::setprecision(17);
  std::cout << "fixed_step_s=" << simulator.fixed_step_s() << '\n';

  std::uint64_t total_steps = 0;
  for (std::size_t frame_index = 0; frame_index < input_stream.size(); ++frame_index) {
    const auto report =
        simulator.advance(input_stream[frame_index].elapsed_time_s, input_stream[frame_index].controls);
    total_steps = report.total_steps;
    std::cout << "frame=" << frame_index
              << " caller_delta_s=" << input_stream[frame_index].elapsed_time_s
              << " steps=" << report.steps_executed
              << " remaining_accumulator_s=" << report.remaining_accumulator_s << '\n';
  }

  const auto final_hash = flying::core_sim::hash_state(simulator.state());
  std::cout << "total_steps=" << total_steps << '\n';
  std::cout << "state_hash=0x" << std::hex << std::setw(16) << std::setfill('0') << final_hash
            << std::dec << '\n';

  return 0;
}
