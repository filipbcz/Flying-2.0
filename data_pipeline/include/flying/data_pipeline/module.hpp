#pragma once

namespace flying::data_pipeline {

struct ModuleBoundary {
  const char* name;
  const char* responsibility;
  bool runtime_network_required;
};

ModuleBoundary describe_module() noexcept;

} // namespace flying::data_pipeline
