#include "flying/data_pipeline/module.hpp"

namespace flying::data_pipeline {

ModuleBoundary describe_module() noexcept {
  return {
    "DataPipeline",
    "Native boundary for future offline GIS and package-processing tools.",
    false,
  };
}

} // namespace flying::data_pipeline
