#include "flying/core_sim/module.hpp"

namespace flying::core_sim {

ModuleBoundary describe_module() noexcept {
  return {
    "CoreSim",
    "Standalone native boundary for future fixed-step flight dynamics.",
    false,
  };
}

} // namespace flying::core_sim
