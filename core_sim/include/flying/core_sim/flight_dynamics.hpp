#pragma once

#include "flying/core_sim/fixed_step.hpp"
#include "flying/core_sim/math.hpp"
#include "flying/core_sim/units.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace flying::core_sim {

struct AircraftConfigurationIdentity {
  std::string backend;
  std::string model_name;
  std::string model_version;
  std::string data_root;
  std::string source_license;
};

struct FlightDynamicsInitialCondition {
  double latitude_deg{49.2};
  double longitude_deg{16.6};
  double altitude_m{1'500.0};
  double terrain_elevation_m{};

  Vector3d body_velocity_mps{35.0, 0.0, 0.0};
  Vector3d angular_velocity_body_radps{};

  double roll_rad{};
  double pitch_rad{};
  double heading_rad{};
};

struct AircraftControlInputSample {
  double aileron_norm{};
  double elevator_norm{};
  double rudder_norm{};
  double throttle_norm{};
  double flaps_norm{};
  double brake_left_norm{};
  double brake_right_norm{};
  double mixture_norm{1.0};
  double propeller_norm{1.0};
  double elevator_trim_norm{};
  double aileron_trim_norm{};
  double rudder_trim_norm{};
  bool engine_run_switch{true};
  bool engine_starter_engaged{};
  bool magnetos_on{true};
};

struct FlightDynamicsState {
  double simulation_time_s{};
  std::uint64_t step_index{};

  double latitude_deg{};
  double longitude_deg{};
  double altitude_m{};
  double terrain_elevation_m{};

  Vector3d ecef_position_m{};
  Vector3d ecef_velocity_mps{};
  Vector3d ned_velocity_mps{};
  Vector3d body_velocity_mps{};
  Quaterniond body_to_ecef{};

  Vector3d euler_rad{};
  Vector3d angular_velocity_body_radps{};
  Vector3d total_force_body_n{};
  Vector3d total_moment_body_nm{};

  double angle_of_attack_rad{};
  double sideslip_rad{};
  double mach{};
  double calibrated_airspeed_mps{};
  double engine_rpm{};
  double propeller_thrust_n{};
  double fuel_flow_kgps{};
  double fuel_quantity_kg{};
};

struct FlightDynamicsStepRecord {
  std::uint64_t step_index{};
  double fixed_step_s{};
  AircraftConfigurationIdentity aircraft{};
  AircraftControlInputSample controls{};
  FlightDynamicsState state{};
  std::uint64_t state_hash{};
  std::uint64_t record_hash{};
};

struct FlightDynamicsAdvanceReport {
  std::uint32_t steps_executed{};
  double fixed_step_s{};
  double consumed_time_s{};
  double remaining_accumulator_s{};
  std::uint64_t total_steps{};
  bool has_step_record{};
  FlightDynamicsStepRecord last_step_record{};
  std::vector<FlightDynamicsStepRecord> step_records{};
};

class IFlightDynamicsBackend {
public:
  virtual ~IFlightDynamicsBackend() = default;

  [[nodiscard]] virtual const AircraftConfigurationIdentity& aircraft() const noexcept = 0;
  [[nodiscard]] virtual const FlightDynamicsState& state() const noexcept = 0;
  [[nodiscard]] virtual const FlightDynamicsStepRecord& last_step_record() const noexcept = 0;

  virtual void reset(const FlightDynamicsInitialCondition& initial_condition) = 0;
  virtual FlightDynamicsStepRecord step_fixed(double fixed_step_s,
                                              const AircraftControlInputSample& controls) = 0;
};

class FlightDynamicsStepper {
public:
  explicit FlightDynamicsStepper(std::unique_ptr<IFlightDynamicsBackend> backend,
                                 double fixed_step_s = kFixedStepSeconds);

  [[nodiscard]] const AircraftConfigurationIdentity& aircraft() const noexcept;
  [[nodiscard]] const FlightDynamicsState& state() const noexcept;
  [[nodiscard]] const FlightDynamicsStepRecord& last_step_record() const noexcept;
  [[nodiscard]] double fixed_step_s() const noexcept;

  void reset(const FlightDynamicsInitialCondition& initial_condition = {});
  FlightDynamicsAdvanceReport advance(double caller_delta_s, const AircraftControlInputSample& controls);

private:
  FixedStepAccumulator accumulator_;
  std::unique_ptr<IFlightDynamicsBackend> backend_;
};

void validate_flight_dynamics_initial_condition(const FlightDynamicsInitialCondition& initial_condition);
void validate_aircraft_controls(const AircraftControlInputSample& controls);

[[nodiscard]] std::uint64_t hash_flight_dynamics_state(const FlightDynamicsState& state) noexcept;
[[nodiscard]] std::uint64_t hash_flight_dynamics_step_record(
    const FlightDynamicsStepRecord& record) noexcept;

} // namespace flying::core_sim
