#include "flying/core_sim/simulator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "performance_timing_test: " << message << '\n';
    std::exit(1);
  }
}

void ten_hour_240hz_soak_has_no_missed_steps() {
  flying::core_sim::CoreSimulator simulator{
      flying::core_sim::RigidBodyParameters{975.0, {875.0, 1'060.0, 1'420.0}}};

  constexpr double kCallerFrameSeconds = 1.0 / 60.0;
  constexpr double kDurationSeconds = 10.0 * 60.0 * 60.0;
  constexpr std::uint64_t kCallerFrames =
      static_cast<std::uint64_t>(kDurationSeconds / kCallerFrameSeconds);
  constexpr std::uint64_t kExpectedTotalSteps =
      static_cast<std::uint64_t>(kDurationSeconds * 240.0);

  std::uint64_t missed_steps = 0;
  std::uint32_t max_steps_per_frame = 0;
  for (std::uint64_t frame = 0; frame < kCallerFrames; ++frame) {
    const flying::core_sim::ControlInputSample input{
        {1'100.0 + std::sin(static_cast<double>(frame) * 0.0004) * 50.0, -8.0, 72.0},
        {-3.0, 11.0, -5.0}};
    const flying::core_sim::AdvanceReport report =
        simulator.advance(kCallerFrameSeconds, input);
    max_steps_per_frame = std::max(max_steps_per_frame, report.steps_executed);
    if (report.steps_executed != 4) {
      missed_steps += report.steps_executed < 4 ? 4 - report.steps_executed : 0;
    }
  }

  require(missed_steps == 0, "60 Hz approved route must execute four 240 Hz steps per frame");
  require(max_steps_per_frame == 4, "CoreSim should not burst above the 60 Hz frame step budget");
  require(simulator.state().step_index == kExpectedTotalSteps,
          "ten-hour soak step index drifted from the 240 Hz budget");
  require(std::abs(simulator.state().simulation_time_s - kDurationSeconds) < 1.0e-6,
          "ten-hour soak simulation time drifted");
}

} // namespace

int main() {
  ten_hour_240hz_soak_has_no_missed_steps();
  return 0;
}
