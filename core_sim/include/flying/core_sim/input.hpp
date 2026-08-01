#pragma once

#include "flying/core_sim/math.hpp"

namespace flying::core_sim {

struct ControlInputSample {
  Vector3d force_body_n{};
  Vector3d moment_body_nm{};
};

struct CallerFrameInput {
  double elapsed_time_s{};
  ControlInputSample controls{};
};

} // namespace flying::core_sim
