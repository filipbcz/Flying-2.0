#pragma once

namespace flying::core_sim {

struct ModuleBoundary {
  const char* name;
  const char* responsibility;
  bool runtime_network_required;
};

ModuleBoundary describe_module() noexcept;

} // namespace flying::core_sim
