#include "flying/core_sim/module.hpp"

namespace flying::core_sim {

ModuleBoundary describe_module() noexcept {
  return {
    "CoreSim",
    "Standalone fixed-step native boundary for deterministic flight dynamics.",
    false,
  };
}

} // namespace flying::core_sim
