#include "flying/core_sim/module.hpp"
#include "flying/data_pipeline/module.hpp"
#include "flying/geo_terrain/module.hpp"

#include <cassert>
#include <cstring>

int main() {
  const auto core_sim = flying::core_sim::describe_module();
  const auto geo_terrain = flying::geo_terrain::describe_module();
  const auto data_pipeline = flying::data_pipeline::describe_module();

  assert(std::strcmp(core_sim.name, "CoreSim") == 0);
  assert(std::strcmp(geo_terrain.name, "GeoTerrain") == 0);
  assert(std::strcmp(data_pipeline.name, "DataPipeline") == 0);

  assert(!core_sim.runtime_network_required);
  assert(!geo_terrain.runtime_network_required);
  assert(!data_pipeline.runtime_network_required);

  return 0;
}
