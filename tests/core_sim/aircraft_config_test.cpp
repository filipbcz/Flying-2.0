#include "flying/core_sim/aircraft_config.hpp"
#include "flying/core_sim/determinism.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

using flying::core_sim::AircraftConfiguration;
using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::AircraftLoadout;
using flying::core_sim::CoreSimulator;
using flying::core_sim::hash_state;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

std::filesystem::path repo_root() {
#ifdef FLYING_REPO_SOURCE_DIR
  return std::filesystem::path{FLYING_REPO_SOURCE_DIR};
#else
  return std::filesystem::current_path();
#endif
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

AircraftConfiguration load_config() {
  const auto loaded =
      flying::core_sim::load_aircraft_configuration(flying::core_sim::default_aircraft_config_path());
  if (!loaded.loaded) {
    const std::string error = loaded.errors.empty() ? "unknown error" : loaded.errors.front();
    throw std::runtime_error("production aircraft configuration must load: " + error);
  }
  return loaded.configuration;
}

void schema_requires_production_aircraft_sections() {
  const std::filesystem::path schema_path =
      repo_root() / "core_sim" / "schemas" / "aircraft-config.schema.json";
  const std::string schema = read_text(schema_path);

  for (const char* required : {
           "\"geometry\"",
           "\"massBalance\"",
           "\"aerodynamics\"",
           "\"engine\"",
           "\"propeller\"",
           "\"actuators\"",
           "\"landingGear\"",
           "\"brakes\"",
           "\"sourceReferences\"",
           "\"validation\"",
       }) {
    require(schema.find(required) != std::string::npos,
            "aircraft schema must require every production aircraft section");
  }
  require(schema.find("\"status\": {\"const\": \"unvalidated\"}") != std::string::npos,
          "aircraft schema must keep the model explicitly unvalidated");
}

void production_aircraft_data_loads_with_provenance() {
  const AircraftConfiguration configuration = load_config();

  require(configuration.schema_version == "flying.aircraft-config.v1",
          "aircraft config schema version must be recorded");
  require(configuration.validation_status == "unvalidated",
          "aircraft config must stay unvalidated");
  require(configuration.validation_suite_status == "not_run",
          "aircraft validation suite must not be marked passed");
  require(configuration.source_references.size() >= 3,
          "aircraft config must include source references");
  require(configuration.geometry.wing_area_m2.metadata.unit == "square_meter",
          "geometry values must carry units");
  require(configuration.mass_balance.empty_inertia_kg_m2.value.ixx > 0.0,
          "mass model must include a full inertia tensor");
  require(configuration.aerodynamics.tables.size() >= 12,
          "aerodynamic tables must be present");
  require(configuration.engine.tables.size() >= 3,
          "engine maps must be present");
  require(configuration.propeller.tables.size() >= 2,
          "propeller maps must be present");
  require(configuration.actuators.size() >= 4,
          "actuator parameters must be present");
  require(configuration.landing_gear.size() >= 3,
          "landing gear contacts must be present");
  require(configuration.brakes.size() >= 2,
          "brake parameters must be present");
}

void validation_rejects_missing_provenance_and_validated_status() {
  AircraftConfiguration configuration = load_config();

  configuration.geometry.wing_area_m2.metadata.source_refs.clear();
  require(!flying::core_sim::validate_aircraft_configuration(configuration).empty(),
          "aircraft validation must reject data without source references");

  configuration = load_config();
  configuration.validation_status = "validated";
  require(!flying::core_sim::validate_aircraft_configuration(configuration).empty(),
          "aircraft validation must reject premature validated status");
}

void mass_balance_loadouts_update_core_sim_state() {
  const AircraftConfiguration configuration = load_config();
  CoreSimulator simulator;

  const AircraftLoadout default_loadout =
      flying::core_sim::make_default_aircraft_loadout(configuration);
  flying::core_sim::apply_aircraft_loadout(simulator, configuration, default_loadout);

  const auto default_state = simulator.state().aircraft_mass_balance;
  require_near(default_state.total_mass_kg, 842.4, 1.0e-9,
               "default aircraft loadout total mass must be computed");
  require_near(default_state.fuel_mass_kg, 80.0, 1.0e-9,
               "default aircraft loadout fuel mass must be computed");
  require_near(default_state.payload_mass_kg, 82.0, 1.0e-9,
               "default aircraft loadout payload mass must be computed");
  require_near(simulator.parameters().mass_kg, default_state.total_mass_kg, 0.0,
               "CoreSim integration mass must follow aircraft mass balance");
  require_near(simulator.parameters().inertia_diagonal_kg_m2.x,
               default_state.inertia_tensor_kg_m2.ixx,
               0.0,
               "CoreSim roll inertia must follow aircraft mass balance");
  const std::uint64_t default_hash = hash_state(simulator.state());

  AircraftLoadout changed_loadout = default_loadout;
  for (auto& station : changed_loadout.fuel) {
    if (station.station_id == "left_wing") {
      station.mass_kg = 60.0;
    } else if (station.station_id == "right_wing") {
      station.mass_kg = 20.0;
    }
  }
  for (auto& station : changed_loadout.payload) {
    if (station.station_id == "copilot") {
      station.mass_kg = 75.0;
    } else if (station.station_id == "baggage") {
      station.mass_kg = 20.0;
    }
  }

  flying::core_sim::apply_aircraft_loadout(simulator, configuration, changed_loadout);
  const auto changed_state = simulator.state().aircraft_mass_balance;
  require_near(changed_state.total_mass_kg, 937.4, 1.0e-9,
               "changed aircraft loadout total mass must be computed");
  require(changed_state.center_of_gravity_body_m.x < default_state.center_of_gravity_body_m.x,
          "payload and fuel changes must update CoreSim CG");
  require(hash_state(simulator.state()) != default_hash,
          "mass-balance changes must affect authoritative state hashes");
}

void invalid_loadouts_are_rejected() {
  const AircraftConfiguration configuration = load_config();
  AircraftLoadout loadout = flying::core_sim::make_default_aircraft_loadout(configuration);
  loadout.fuel.front().mass_kg = 1000.0;

  bool threw = false;
  try {
    (void)flying::core_sim::compute_aircraft_mass_balance(configuration, loadout);
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  require(threw, "validated loadout API must reject fuel outside station capacity");
}

void aerodynamic_engine_and_propeller_tables_evaluate() {
  const AircraftConfiguration configuration = load_config();
  AircraftControlInputSample controls{};
  controls.elevator_norm = -0.2;
  controls.aileron_norm = 0.3;
  controls.rudder_norm = 0.1;
  controls.throttle_norm = 0.75;
  controls.flaps_norm = 0.5;
  controls.mixture_norm = 1.0;
  controls.propeller_norm = 1.0;

  const auto clean =
      flying::core_sim::evaluate_aerodynamic_coefficients(configuration, {}, 0.08, 0.03);
  const auto configured =
      flying::core_sim::evaluate_aerodynamic_coefficients(configuration, controls, 0.08, 0.03);
  require(configured.lift > clean.lift,
          "flap aerodynamic tables must contribute to lift coefficient");
  require(configured.drag > clean.drag,
          "flap aerodynamic tables must contribute to drag coefficient");
  require(std::abs(configured.roll_moment) > std::abs(clean.roll_moment),
          "aileron aerodynamic table must contribute to roll moment");

  const double engine_power_w =
      flying::core_sim::evaluate_engine_power_w(configuration, 0.8, 2400.0);
  require(engine_power_w > configuration.engine.rated_power_w.value * 0.5,
          "engine maps must produce usable partial-throttle power");

  const auto propeller =
      flying::core_sim::evaluate_propeller_coefficients(configuration, 0.6);
  require(propeller.thrust > 0.0 && propeller.power > 0.0,
          "propeller maps must evaluate thrust and power coefficients");
}

} // namespace

int main() {
  schema_requires_production_aircraft_sections();
  production_aircraft_data_loads_with_provenance();
  validation_rejects_missing_provenance_and_validated_status();
  mass_balance_loadouts_update_core_sim_state();
  invalid_loadouts_are_rejected();
  aerodynamic_engine_and_propeller_tables_evaluate();
  return 0;
}
