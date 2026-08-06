#pragma once

#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/simulator.hpp"
#include "flying/core_sim/state.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace flying::core_sim {

inline constexpr std::string_view kAircraftConfigSchemaVersion = "flying.aircraft-config.v1";
inline constexpr std::string_view kAircraftConfigUnvalidatedStatus = "unvalidated";

struct AircraftSourceReference {
  std::string id;
  std::string title;
  std::string document;
  std::string license;
  std::string permitted_use;
  std::string provenance;
  std::string confidence;
  std::vector<std::string> used_for;
};

struct AircraftValueMetadata {
  std::string unit;
  std::vector<std::string> source_refs;
  std::string confidence;
  double validity_min{};
  double validity_max{};
};

struct AircraftVectorMetadata {
  std::string unit;
  std::vector<std::string> source_refs;
  std::string confidence;
  Vector3d validity_min{};
  Vector3d validity_max{};
};

struct AircraftScalar {
  double value{};
  AircraftValueMetadata metadata{};
};

struct AircraftVectorValue {
  Vector3d value{};
  AircraftVectorMetadata metadata{};
};

struct AircraftInertiaTensorValue {
  AircraftInertiaTensor value{};
  std::string unit;
  std::vector<std::string> source_refs;
  std::string confidence;
  AircraftInertiaTensor validity_min{};
  AircraftInertiaTensor validity_max{};
};

struct AircraftGeometryModel {
  std::string coordinate_frame;
  AircraftScalar wing_area_m2{};
  AircraftScalar wingspan_m{};
  AircraftScalar mean_aerodynamic_chord_m{};
  AircraftScalar fuselage_length_m{};
  AircraftScalar horizontal_tail_area_m2{};
  AircraftScalar horizontal_tail_arm_m{};
  AircraftScalar vertical_tail_area_m2{};
  AircraftScalar vertical_tail_arm_m{};
  AircraftScalar propeller_ground_clearance_m{};
  AircraftVectorValue aerodynamic_reference_point_body_m{};
  AircraftVectorValue visual_reference_point_body_m{};
};

struct AircraftCgEnvelopePoint {
  AircraftScalar mass_kg{};
  AircraftVectorValue forward_limit_body_m{};
  AircraftVectorValue aft_limit_body_m{};
};

struct AircraftFuelStation {
  std::string id;
  std::string display_name;
  AircraftScalar capacity_kg{};
  AircraftScalar unusable_kg{};
  AircraftScalar default_quantity_kg{};
  AircraftVectorValue position_body_m{};
};

struct AircraftPayloadStation {
  std::string id;
  std::string display_name;
  AircraftScalar max_mass_kg{};
  AircraftScalar default_mass_kg{};
  AircraftVectorValue position_body_m{};
};

struct AircraftMassBalanceModel {
  std::string inertia_reference;
  AircraftScalar empty_mass_kg{};
  AircraftScalar max_takeoff_mass_kg{};
  AircraftVectorValue empty_cg_body_m{};
  AircraftInertiaTensorValue empty_inertia_kg_m2{};
  std::vector<AircraftCgEnvelopePoint> cg_envelope;
  std::vector<AircraftFuelStation> fuel_stations;
  std::vector<AircraftPayloadStation> payload_stations;
};

struct AircraftTablePoint {
  double x{};
  double y{};
};

struct AircraftTable1d {
  std::string id;
  std::string input;
  std::string input_unit;
  std::string output;
  std::string output_unit;
  std::string confidence;
  double validity_min{};
  double validity_max{};
  std::vector<std::string> source_refs;
  std::vector<AircraftTablePoint> points;
};

struct AircraftAerodynamicModel {
  std::string coefficient_frame;
  AircraftScalar alpha_min_rad{};
  AircraftScalar alpha_max_rad{};
  AircraftScalar beta_max_abs_rad{};
  std::vector<AircraftTable1d> tables;
};

struct AircraftEngineModel {
  std::string id;
  std::string type;
  AircraftScalar rated_power_w{};
  AircraftScalar idle_rpm{};
  AircraftScalar max_rpm{};
  AircraftScalar displacement_m3{};
  AircraftScalar fuel_density_kg_per_l{};
  std::vector<AircraftTable1d> tables;
};

struct AircraftPropellerModel {
  std::string id;
  std::string type;
  int blade_count{};
  AircraftScalar diameter_m{};
  AircraftScalar inertia_kg_m2{};
  AircraftScalar min_pitch_rad{};
  AircraftScalar max_pitch_rad{};
  std::vector<AircraftTable1d> tables;
};

struct AircraftActuatorModel {
  std::string id;
  std::string surface;
  std::string command;
  AircraftScalar min_deflection_rad{};
  AircraftScalar max_deflection_rad{};
  AircraftScalar slew_rate_radps{};
  AircraftScalar time_constant_s{};
};

struct AircraftLandingGearContact {
  std::string id;
  std::string type;
  std::string brake_group;
  bool retractable{};
  AircraftVectorValue position_body_m{};
  AircraftScalar static_friction{};
  AircraftScalar dynamic_friction{};
  AircraftScalar rolling_friction{};
  AircraftScalar spring_coefficient_n_per_m{};
  AircraftScalar damping_coefficient_n_s_per_m{};
  AircraftScalar max_steer_rad{};
};

struct AircraftBrakeModel {
  std::string id;
  std::string wheel;
  std::string brake_group;
  AircraftScalar max_torque_nm{};
  AircraftScalar response_time_s{};
  AircraftScalar parking_brake_hold_norm{};
};

struct AircraftConfiguration {
  std::string schema_version;
  AircraftConfigurationIdentity identity{};
  std::string display_name;
  std::string validation_status;
  std::string validation_suite;
  std::string validation_suite_status;
  std::string license_spdx;
  std::string license_notice;
  std::string provenance_summary;
  std::vector<AircraftSourceReference> source_references;
  AircraftGeometryModel geometry{};
  AircraftMassBalanceModel mass_balance{};
  AircraftAerodynamicModel aerodynamics{};
  AircraftEngineModel engine{};
  AircraftPropellerModel propeller{};
  std::vector<AircraftActuatorModel> actuators;
  std::vector<AircraftLandingGearContact> landing_gear;
  std::vector<AircraftBrakeModel> brakes;
};

struct AircraftConfigurationLoadResult {
  bool loaded{};
  AircraftConfiguration configuration{};
  std::vector<std::string> errors;
};

struct AircraftLoadout {
  struct StationQuantity {
    std::string station_id;
    double mass_kg{};
  };

  std::vector<StationQuantity> fuel;
  std::vector<StationQuantity> payload;
};

struct AircraftAerodynamicCoefficients {
  double lift{};
  double drag{};
  double side_force{};
  double roll_moment{};
  double pitch_moment{};
  double yaw_moment{};
};

struct AircraftPropellerCoefficients {
  double thrust{};
  double power{};
};

[[nodiscard]] std::filesystem::path default_aircraft_config_path();
[[nodiscard]] AircraftConfigurationLoadResult load_aircraft_configuration(
    const std::filesystem::path& path);
[[nodiscard]] std::vector<std::string> validate_aircraft_configuration(
    const AircraftConfiguration& configuration);

[[nodiscard]] AircraftLoadout make_default_aircraft_loadout(
    const AircraftConfiguration& configuration);
[[nodiscard]] AircraftMassBalanceState compute_aircraft_mass_balance(
    const AircraftConfiguration& configuration,
    const AircraftLoadout& loadout);
void apply_aircraft_loadout(CoreSimulator& simulator,
                            const AircraftConfiguration& configuration,
                            const AircraftLoadout& loadout);

[[nodiscard]] double evaluate_aircraft_table(const AircraftTable1d& table, double x);
[[nodiscard]] const AircraftTable1d& require_aircraft_table(
    const AircraftConfiguration& configuration,
    std::string_view table_id);
[[nodiscard]] AircraftAerodynamicCoefficients evaluate_aerodynamic_coefficients(
    const AircraftConfiguration& configuration,
    const AircraftControlInputSample& controls,
    double angle_of_attack_rad,
    double sideslip_rad);
[[nodiscard]] double evaluate_engine_power_w(const AircraftConfiguration& configuration,
                                             double throttle_norm,
                                             double rpm);
[[nodiscard]] AircraftPropellerCoefficients evaluate_propeller_coefficients(
    const AircraftConfiguration& configuration,
    double advance_ratio);

} // namespace flying::core_sim
