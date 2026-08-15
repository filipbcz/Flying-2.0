#include "AircraftSystems.h"

namespace flying::core_sim {

static_assert(sizeof(PublicAircraftSystemsModel) == sizeof(AircraftSystemsModel),
              "Public aircraft systems facade must expose the stateful CoreSim sensor model.");

} // namespace flying::core_sim
