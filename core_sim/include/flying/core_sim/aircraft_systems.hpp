#pragma once

#include "flying/core_sim/flight_dynamics.hpp"
#include "flying/core_sim/math.hpp"
#include "flying/core_sim/weather.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace flying::core_sim {

struct SensorLag {
  double time_constant_s{0.25};
  double value{};
  bool initialized{};

  [[nodiscard]] double update(double target, double dt_s) noexcept;
  void reset(double target = 0.0) noexcept;
};

struct FailureStateModel {
  bool battery_failed{};
  bool alternator_failed{};
  bool avionics_bus_failed{};
  bool fuel_left_tank_blocked{};
  bool fuel_right_tank_blocked{};
  bool engine_driven_fuel_pump_failed{};
  bool electric_fuel_pump_failed{};
  bool vacuum_pump_failed{};
  bool standby_vacuum_pump_failed{};
  bool pitot_blocked{};
  bool static_port_blocked{};
  bool pitot_heat_failed{};
  bool gps_failed{};
  bool engine_sensor_power_failed{};
};

struct AircraftSystemsSwitches {
  bool battery_master_on{true};
  bool alternator_on{true};
  bool avionics_master_on{true};
  bool pitot_heat_on{};
  bool electric_fuel_pump_on{};
  bool standby_vacuum_pump_on{};
};

struct ElectricalConsumerState {
  std::string id;
  double demand_w{};
  bool switch_on{true};
  bool failed{};
  bool powered{};
};

struct ElectricalSystemSnapshot {
  double bus_voltage_v{};
  double battery_charge_norm{1.0};
  double alternator_output_w{};
  double load_w{};
  bool battery_online{};
  bool alternator_online{};
  bool avionics_bus_powered{};
  std::vector<ElectricalConsumerState> consumers;
};

class ElectricalSystem {
public:
  void set_consumers(std::vector<ElectricalConsumerState> consumers);
  [[nodiscard]] const ElectricalSystemSnapshot& snapshot() const noexcept;
  [[nodiscard]] bool consumer_powered(const std::string& id) const noexcept;
  void reset(double battery_charge_norm = 1.0);
  void step(double dt_s,
            double engine_rpm,
            const AircraftSystemsSwitches& switches,
            const FailureStateModel& failures);

private:
  ElectricalSystemSnapshot snapshot_{};
};

enum class FuelTankSelector {
  left,
  right,
  both,
  off,
};

struct FuelTankState {
  double left_quantity_kg{40.0};
  double right_quantity_kg{40.0};
  double left_capacity_kg{50.0};
  double right_capacity_kg{50.0};
  FuelTankSelector selector{FuelTankSelector::both};
};

struct FuelSystemSnapshot {
  FuelTankState tanks{};
  double requested_flow_kgps{};
  double delivered_flow_kgps{};
  double fuel_pressure_kpa{};
  bool engine_driven_pump_online{};
  bool electric_pump_online{};
  bool engine_fuel_starved{};
};

class FuelSystem {
public:
  [[nodiscard]] const FuelSystemSnapshot& snapshot() const noexcept;
  void reset(FuelTankState tanks = {});
  void set_selector(FuelTankSelector selector) noexcept;
  void step(double dt_s,
            double requested_flow_kgps,
            double engine_rpm,
            bool electric_pump_powered,
            const AircraftSystemsSwitches& switches,
            const FailureStateModel& failures);

private:
  FuelSystemSnapshot snapshot_{};
};

struct VacuumConsumerState {
  std::string id;
  bool switch_on{true};
  bool failed{};
  bool powered{};
};

struct VacuumSystemSnapshot {
  double suction_inhg{};
  bool engine_pump_online{};
  bool standby_pump_online{};
  std::vector<VacuumConsumerState> consumers;
};

class VacuumSystem {
public:
  void set_consumers(std::vector<VacuumConsumerState> consumers);
  [[nodiscard]] const VacuumSystemSnapshot& snapshot() const noexcept;
  [[nodiscard]] bool consumer_powered(const std::string& id) const noexcept;
  void reset() noexcept;
  void step(double dt_s,
            double engine_rpm,
            bool standby_pump_powered,
            const AircraftSystemsSwitches& switches,
            const FailureStateModel& failures);

private:
  VacuumSystemSnapshot snapshot_{};
  SensorLag suction_lag_{1.2};
};

struct PitotStaticSettings {
  double qnh_pa{101'325.0};
  double qfe_reference_altitude_m{};
};

struct PitotStaticSnapshot {
  AtmosphereSample atmosphere{};
  double pitot_pressure_pa{101'325.0};
  double static_pressure_pa{101'325.0};
  double indicated_airspeed_mps{};
  double indicated_altitude_m{};
  double vertical_speed_mps{};
  bool pitot_blocked{};
  bool static_blocked{};
  bool icing_present{};
};

class PitotStaticSystem {
public:
  [[nodiscard]] const PitotStaticSnapshot& snapshot() const noexcept;
  void reset(double altitude_m = 0.0, double airspeed_mps = 0.0);
  void set_settings(PitotStaticSettings settings) noexcept;
  void step(double dt_s,
            const FlightDynamicsState& truth,
            AtmosphereSample atmosphere,
            double icing_severity_norm,
            bool pitot_heat_powered,
            const FailureStateModel& failures);

private:
  PitotStaticSettings settings_{};
  PitotStaticSnapshot snapshot_{};
  SensorLag pitot_lag_{0.35};
  SensorLag static_lag_{0.60};
  SensorLag vsi_lag_{5.0};
  double previous_indicated_altitude_m_{};
};

struct CompassSnapshot {
  double magnetic_heading_rad{};
  bool valid{true};
};

class CompassSystem {
public:
  [[nodiscard]] const CompassSnapshot& snapshot() const noexcept;
  void reset(double heading_rad = 0.0) noexcept;
  void step(double dt_s, double true_heading_rad, double magnetic_variation_rad);

private:
  CompassSnapshot snapshot_{};
  SensorLag heading_lag_{1.5};
};

struct GyroSnapshot {
  double attitude_roll_rad{};
  double attitude_pitch_rad{};
  double heading_rad{};
  bool attitude_valid{};
  bool heading_valid{};
};

class GyroSystem {
public:
  [[nodiscard]] const GyroSnapshot& snapshot() const noexcept;
  void reset() noexcept;
  void step(double dt_s,
            const FlightDynamicsState& truth,
            bool attitude_gyro_powered,
            bool heading_gyro_powered);

private:
  GyroSnapshot snapshot_{};
  SensorLag roll_lag_{0.25};
  SensorLag pitch_lag_{0.25};
  SensorLag heading_lag_{0.50};
};

struct GpsSnapshot {
  double latitude_deg{};
  double longitude_deg{};
  double altitude_m{};
  double ground_speed_mps{};
  double track_rad{};
  bool valid{};
};

class GpsSystem {
public:
  [[nodiscard]] const GpsSnapshot& snapshot() const noexcept;
  void reset() noexcept;
  void step(double dt_s,
            const FlightDynamicsState& truth,
            bool avionics_powered,
            const FailureStateModel& failures);

private:
  GpsSnapshot snapshot_{};
  SensorLag latitude_lag_{1.0};
  SensorLag longitude_lag_{1.0};
  SensorLag altitude_lag_{1.0};
};

struct EngineInstrumentSnapshot {
  double rpm{};
  double manifold_pressure_kpa{};
  double oil_temperature_k{};
  double cylinder_head_temperature_k{};
  double exhaust_gas_temperature_k{};
  double fuel_flow_kgps{};
  bool valid{};
  bool fuel_starved{};
};

class EngineInstrumentSystem {
public:
  [[nodiscard]] const EngineInstrumentSnapshot& snapshot() const noexcept;
  void reset() noexcept;
  void step(double dt_s,
            double commanded_throttle_norm,
            double engine_rpm,
            double ambient_pressure_pa,
            const FuelSystemSnapshot& fuel,
            bool sensor_powered,
            const FailureStateModel& failures);

private:
  EngineInstrumentSnapshot snapshot_{};
  SensorLag rpm_lag_{0.30};
  SensorLag manifold_lag_{0.45};
  SensorLag oil_lag_{12.0};
  SensorLag cht_lag_{6.0};
  SensorLag egt_lag_{2.0};
  SensorLag fuel_flow_lag_{0.50};
};

struct AircraftSystemsInput {
  FlightDynamicsState truth{};
  AircraftControlInputSample controls{};
  double outside_air_temperature_k{288.15};
  WeatherSample weather{};
  bool weather_valid{};
  double icing_severity_norm{};
  double magnetic_variation_rad{};
  double engine_rpm{};
};

struct InstrumentData {
  double indicated_airspeed_mps{};
  double indicated_altitude_m{};
  double vertical_speed_mps{};
  double magnetic_heading_rad{};
  double attitude_roll_rad{};
  double attitude_pitch_rad{};
  double gyro_heading_rad{};
  GpsSnapshot gps{};
  EngineInstrumentSnapshot engine{};
  ElectricalSystemSnapshot electrical{};
  FuelSystemSnapshot fuel{};
  VacuumSystemSnapshot vacuum{};
  PitotStaticSnapshot pitot_static{};
  WeatherSample weather{};
  std::uint64_t sequence{};
};

class AircraftSystemsModel {
public:
  AircraftSystemsModel();

  [[nodiscard]] const InstrumentData& instruments() const noexcept;
  [[nodiscard]] ElectricalSystem& electrical() noexcept;
  [[nodiscard]] FuelSystem& fuel() noexcept;
  [[nodiscard]] VacuumSystem& vacuum() noexcept;
  [[nodiscard]] PitotStaticSystem& pitot_static() noexcept;
  [[nodiscard]] FailureStateModel& failures() noexcept;
  [[nodiscard]] AircraftSystemsSwitches& switches() noexcept;

  void reset();
  InstrumentData step(double dt_s, const AircraftSystemsInput& input);

private:
  AircraftSystemsSwitches switches_{};
  FailureStateModel failures_{};
  ElectricalSystem electrical_{};
  FuelSystem fuel_{};
  VacuumSystem vacuum_{};
  PitotStaticSystem pitot_static_{};
  CompassSystem compass_{};
  GyroSystem gyro_{};
  GpsSystem gps_{};
  EngineInstrumentSystem engine_{};
  InstrumentData instruments_{};
};

[[nodiscard]] AtmosphereSample sample_standard_atmosphere(double altitude_m,
                                                         double qnh_pa,
                                                         double temperature_k);

} // namespace flying::core_sim
