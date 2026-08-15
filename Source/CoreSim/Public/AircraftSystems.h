#pragma once

#include "flying/core_sim/aircraft_systems.hpp"

namespace flying::core_sim {

using PublicAircraftSystemsInput = AircraftSystemsInput;
using PublicAircraftSystemsModel = AircraftSystemsModel;
using PublicAircraftSystemsSwitches = AircraftSystemsSwitches;
using PublicElectricalSystemSnapshot = ElectricalSystemSnapshot;
using PublicFailureStateModel = FailureStateModel;
using PublicFuelSystemSnapshot = FuelSystemSnapshot;
using PublicGpsSnapshot = GpsSnapshot;
using PublicInstrumentData = InstrumentData;
using PublicPitotStaticSnapshot = PitotStaticSnapshot;
using PublicVacuumSystemSnapshot = VacuumSystemSnapshot;

} // namespace flying::core_sim
