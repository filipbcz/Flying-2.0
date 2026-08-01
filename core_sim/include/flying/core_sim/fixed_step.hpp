#pragma once

#include "flying/core_sim/units.hpp"

#include <cstdint>

namespace flying::core_sim {

struct FixedStepAdvance {
  std::uint32_t steps_executed{};
  double consumed_time_s{};
  double remaining_accumulator_s{};
  std::uint64_t total_steps{};
};

class FixedStepAccumulator {
public:
  explicit FixedStepAccumulator(double fixed_step_s = kFixedStepSeconds);

  [[nodiscard]] double fixed_step_s() const noexcept;
  [[nodiscard]] double accumulated_time_s() const noexcept;
  [[nodiscard]] std::uint64_t total_steps() const noexcept;

  void reset() noexcept;
  void add_elapsed_time(double elapsed_time_s);
  [[nodiscard]] bool has_step() const noexcept;
  void consume_step() noexcept;

private:
  double fixed_step_s_;
  double accumulated_time_s_{};
  std::uint64_t total_steps_{};
};

} // namespace flying::core_sim
