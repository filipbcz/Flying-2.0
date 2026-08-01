#include "flying/core_sim/fixed_step.hpp"

#include <cmath>
#include <stdexcept>

namespace flying::core_sim {
namespace {

[[nodiscard]] double step_tolerance(double fixed_step_s) noexcept {
  return fixed_step_s * 1.0e-12;
}

} // namespace

FixedStepAccumulator::FixedStepAccumulator(double fixed_step_s)
    : fixed_step_s_(fixed_step_s) {
  if (fixed_step_s_ <= 0.0 || !std::isfinite(fixed_step_s_)) {
    throw std::invalid_argument("fixed_step_s must be a positive finite SI duration");
  }
}

double FixedStepAccumulator::fixed_step_s() const noexcept {
  return fixed_step_s_;
}

double FixedStepAccumulator::accumulated_time_s() const noexcept {
  return accumulated_time_s_;
}

std::uint64_t FixedStepAccumulator::total_steps() const noexcept {
  return total_steps_;
}

void FixedStepAccumulator::reset() noexcept {
  accumulated_time_s_ = 0.0;
  total_steps_ = 0;
}

void FixedStepAccumulator::add_elapsed_time(double elapsed_time_s) {
  if (elapsed_time_s < 0.0 || !std::isfinite(elapsed_time_s)) {
    throw std::invalid_argument("elapsed_time_s must be a non-negative finite SI duration");
  }

  accumulated_time_s_ += elapsed_time_s;
  if (!std::isfinite(accumulated_time_s_)) {
    throw std::invalid_argument("accumulated fixed-step time must remain finite");
  }
}

bool FixedStepAccumulator::has_step() const noexcept {
  return accumulated_time_s_ + step_tolerance(fixed_step_s_) >= fixed_step_s_;
}

void FixedStepAccumulator::consume_step() noexcept {
  if (!has_step()) {
    return;
  }

  accumulated_time_s_ -= fixed_step_s_;
  if (accumulated_time_s_ < step_tolerance(fixed_step_s_)) {
    accumulated_time_s_ = 0.0;
  }
  ++total_steps_;
}

} // namespace flying::core_sim
