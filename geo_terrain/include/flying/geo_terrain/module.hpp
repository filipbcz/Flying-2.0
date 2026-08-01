#pragma once

namespace flying::geo_terrain {

struct ModuleBoundary {
  const char* name;
  const char* responsibility;
  bool runtime_network_required;
};

ModuleBoundary describe_module() noexcept;

} // namespace flying::geo_terrain
