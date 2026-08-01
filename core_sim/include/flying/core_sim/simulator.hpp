#pragma once

#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/fixed_step.hpp"
#include "flying/core_sim/input.hpp"
#include "flying/core_sim/state.hpp"

namespace flying::core_sim {

struct AdvanceReport {
  std::uint32_t steps_executed{};
  double fixed_step_s{};
  double consumed_time_s{};
  double remaining_accumulator_s{};
  std::uint64_t total_steps{};
  std::uint64_t state_hash{};
};

class CoreSimulator {
public:
  explicit CoreSimulator(RigidBodyParameters parameters = {});

  [[nodiscard]] const AuthoritativeState& state() const noexcept;
  [[nodiscard]] const RigidBodyParameters& parameters() const noexcept;
  [[nodiscard]] double fixed_step_s() const noexcept;

  void reset(AuthoritativeState state = {}) noexcept;
  AdvanceReport advance(double caller_delta_s, const ControlInputSample& input);
  void integrate_fixed_step(const ControlInputSample& input);

private:
  FixedStepAccumulator accumulator_;
  RigidBodyParameters parameters_;
  AuthoritativeState state_;
};

} // namespace flying::core_sim
