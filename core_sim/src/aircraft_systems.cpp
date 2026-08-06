#include "flying/core_sim/aircraft_systems.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace flying::core_sim {
namespace {

constexpr double kSeaLevelPressurePa = 101'325.0;
constexpr double kSeaLevelTemperatureK = 288.15;
constexpr double kMinPressurePa = 1'000.0;
constexpr double kTwoPi = 6.2831853071795864769;

[[nodiscard]] double clamp(double value, double min_value, double max_value) noexcept {
  return std::max(min_value, std::min(max_value, value));
}

[[nodiscard]] double finite_or(double value, double fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] double first_order_alpha(double time_constant_s, double dt_s) noexcept {
  if (dt_s <= 0.0) {
    return 0.0;
  }
  if (time_constant_s <= 0.0) {
    return 1.0;
  }
  return 1.0 - std::exp(-dt_s / time_constant_s);
}

[[nodiscard]] double wrap_radians(double angle_rad) noexcept {
  double wrapped = std::fmod(angle_rad, kTwoPi);
  if (wrapped < 0.0) {
    wrapped += kTwoPi;
  }
  return wrapped;
}

[[nodiscard]] double shortest_angle_delta(double from_rad, double to_rad) noexcept {
  double delta = std::fmod(to_rad - from_rad + 3.14159265358979323846, kTwoPi);
  if (delta < 0.0) {
    delta += kTwoPi;
  }
  return delta - 3.14159265358979323846;
}

[[nodiscard]] double dynamic_pressure_pa(double density_kgpm3, double speed_mps) noexcept {
  return 0.5 * std::max(0.0, density_kgpm3) * speed_mps * speed_mps;
}

[[nodiscard]] double horizontal_speed(Vector3d velocity) noexcept {
  return std::hypot(velocity.x, velocity.y);
}

[[nodiscard]] double nonnegative(double value) noexcept {
  return std::max(0.0, finite_or(value, 0.0));
}

void require_valid_dt(double dt_s) {
  if (dt_s < 0.0 || !std::isfinite(dt_s)) {
    throw std::invalid_argument("system step dt_s must be a non-negative finite duration");
  }
}

} // namespace

double SensorLag::update(double target, double dt_s) noexcept {
  target = finite_or(target, value);
  if (!initialized) {
    value = target;
    initialized = true;
    return value;
  }

  value += (target - value) * first_order_alpha(time_constant_s, dt_s);
  return value;
}

void SensorLag::reset(double target) noexcept {
  value = finite_or(target, 0.0);
  initialized = true;
}

AtmosphereSample sample_standard_atmosphere(double altitude_m,
                                           double qnh_pa,
                                           double temperature_k) {
  const double finite_altitude_m = finite_or(altitude_m, 0.0);
  return sample_weather_atmosphere(
      finite_altitude_m,
      qnh_pa,
      temperature_k + 0.0065 * finite_altitude_m,
      0.50);
}

void ElectricalSystem::set_consumers(std::vector<ElectricalConsumerState> consumers) {
  snapshot_.consumers = std::move(consumers);
}

const ElectricalSystemSnapshot& ElectricalSystem::snapshot() const noexcept {
  return snapshot_;
}

bool ElectricalSystem::consumer_powered(const std::string& id) const noexcept {
  const auto found = std::find_if(snapshot_.consumers.begin(), snapshot_.consumers.end(),
                                  [&id](const ElectricalConsumerState& consumer) {
                                    return consumer.id == id;
                                  });
  return found != snapshot_.consumers.end() && found->powered;
}

void ElectricalSystem::reset(double battery_charge_norm) {
  auto consumers = std::move(snapshot_.consumers);
  snapshot_ = {};
  snapshot_.consumers = std::move(consumers);
  snapshot_.battery_charge_norm = clamp(battery_charge_norm, 0.0, 1.0);
}

void ElectricalSystem::step(double dt_s,
                            double engine_rpm,
                            const AircraftSystemsSwitches& switches,
                            const FailureStateModel& failures) {
  require_valid_dt(dt_s);
  snapshot_.battery_online =
      switches.battery_master_on && !failures.battery_failed && snapshot_.battery_charge_norm > 0.02;
  snapshot_.alternator_online =
      switches.alternator_on && !failures.alternator_failed && engine_rpm > 900.0;

  snapshot_.alternator_output_w =
      snapshot_.alternator_online ? clamp((engine_rpm - 900.0) / 900.0, 0.0, 1.0) * 700.0 : 0.0;
  snapshot_.bus_voltage_v = snapshot_.alternator_online ? 14.1 :
                            snapshot_.battery_online ? 12.2 * snapshot_.battery_charge_norm : 0.0;
  snapshot_.avionics_bus_powered =
      snapshot_.bus_voltage_v >= 10.5 && switches.avionics_master_on && !failures.avionics_bus_failed;

  snapshot_.load_w = 0.0;
  for (auto& consumer : snapshot_.consumers) {
    consumer.powered =
        snapshot_.bus_voltage_v >= 10.5 && consumer.switch_on && !consumer.failed &&
        (snapshot_.avionics_bus_powered || consumer.id.rfind("avionics.", 0) != 0);
    if (consumer.powered) {
      snapshot_.load_w += nonnegative(consumer.demand_w);
    }
  }

  if (!snapshot_.alternator_online && snapshot_.battery_online && dt_s > 0.0) {
    snapshot_.battery_charge_norm =
        clamp(snapshot_.battery_charge_norm - (snapshot_.load_w / 4'000.0) * (dt_s / 3'600.0),
              0.0,
              1.0);
  } else if (snapshot_.alternator_online && dt_s > 0.0) {
    snapshot_.battery_charge_norm =
        clamp(snapshot_.battery_charge_norm + 0.05 * (dt_s / 3'600.0), 0.0, 1.0);
  }
}

const FuelSystemSnapshot& FuelSystem::snapshot() const noexcept {
  return snapshot_;
}

void FuelSystem::reset(FuelTankState tanks) {
  snapshot_ = {};
  snapshot_.tanks = tanks;
  snapshot_.tanks.left_quantity_kg =
      clamp(snapshot_.tanks.left_quantity_kg, 0.0, snapshot_.tanks.left_capacity_kg);
  snapshot_.tanks.right_quantity_kg =
      clamp(snapshot_.tanks.right_quantity_kg, 0.0, snapshot_.tanks.right_capacity_kg);
}

void FuelSystem::set_selector(FuelTankSelector selector) noexcept {
  snapshot_.tanks.selector = selector;
}

void FuelSystem::step(double dt_s,
                      double requested_flow_kgps,
                      double engine_rpm,
                      bool electric_pump_powered,
                      const AircraftSystemsSwitches& switches,
                      const FailureStateModel& failures) {
  require_valid_dt(dt_s);
  snapshot_.requested_flow_kgps = nonnegative(requested_flow_kgps);
  snapshot_.engine_driven_pump_online =
      engine_rpm > 350.0 && !failures.engine_driven_fuel_pump_failed;
  snapshot_.electric_pump_online =
      switches.electric_fuel_pump_on && electric_pump_powered && !failures.electric_fuel_pump_failed;

  const bool pressure_available = snapshot_.engine_driven_pump_online || snapshot_.electric_pump_online;
  double left_available = failures.fuel_left_tank_blocked ? 0.0 : snapshot_.tanks.left_quantity_kg;
  double right_available = failures.fuel_right_tank_blocked ? 0.0 : snapshot_.tanks.right_quantity_kg;
  if (snapshot_.tanks.selector == FuelTankSelector::left) {
    right_available = 0.0;
  } else if (snapshot_.tanks.selector == FuelTankSelector::right) {
    left_available = 0.0;
  } else if (snapshot_.tanks.selector == FuelTankSelector::off) {
    left_available = 0.0;
    right_available = 0.0;
  }

  const double requested_mass = snapshot_.requested_flow_kgps * dt_s;
  const double available_mass = left_available + right_available;
  const double delivered_mass =
      pressure_available ? std::min(requested_mass, available_mass) : 0.0;
  snapshot_.delivered_flow_kgps = dt_s > 0.0 ? delivered_mass / dt_s : snapshot_.requested_flow_kgps;
  snapshot_.engine_fuel_starved =
      snapshot_.requested_flow_kgps > 1.0e-6 &&
      snapshot_.delivered_flow_kgps < snapshot_.requested_flow_kgps * 0.95;
  snapshot_.fuel_pressure_kpa =
      pressure_available ? (snapshot_.electric_pump_online ? 32.0 : 24.0) : 0.0;

  if (delivered_mass <= 0.0) {
    return;
  }
  if (left_available > 0.0 && right_available > 0.0) {
    const double left_share = left_available / (left_available + right_available);
    snapshot_.tanks.left_quantity_kg =
        std::max(0.0, snapshot_.tanks.left_quantity_kg - delivered_mass * left_share);
    snapshot_.tanks.right_quantity_kg =
        std::max(0.0, snapshot_.tanks.right_quantity_kg - delivered_mass * (1.0 - left_share));
  } else if (left_available > 0.0) {
    snapshot_.tanks.left_quantity_kg =
        std::max(0.0, snapshot_.tanks.left_quantity_kg - delivered_mass);
  } else {
    snapshot_.tanks.right_quantity_kg =
        std::max(0.0, snapshot_.tanks.right_quantity_kg - delivered_mass);
  }
}

void VacuumSystem::set_consumers(std::vector<VacuumConsumerState> consumers) {
  snapshot_.consumers = std::move(consumers);
}

const VacuumSystemSnapshot& VacuumSystem::snapshot() const noexcept {
  return snapshot_;
}

bool VacuumSystem::consumer_powered(const std::string& id) const noexcept {
  const auto found = std::find_if(snapshot_.consumers.begin(), snapshot_.consumers.end(),
                                  [&id](const VacuumConsumerState& consumer) {
                                    return consumer.id == id;
                                  });
  return found != snapshot_.consumers.end() && found->powered;
}

void VacuumSystem::reset() noexcept {
  auto consumers = std::move(snapshot_.consumers);
  snapshot_ = {};
  snapshot_.consumers = std::move(consumers);
  snapshot_.suction_inhg = 0.0;
  snapshot_.engine_pump_online = false;
  snapshot_.standby_pump_online = false;
  suction_lag_.reset(0.0);
}

void VacuumSystem::step(double dt_s,
                        double engine_rpm,
                        bool standby_pump_powered,
                        const AircraftSystemsSwitches& switches,
                        const FailureStateModel& failures) {
  require_valid_dt(dt_s);
  snapshot_.engine_pump_online = engine_rpm > 900.0 && !failures.vacuum_pump_failed;
  snapshot_.standby_pump_online =
      switches.standby_vacuum_pump_on && standby_pump_powered && !failures.standby_vacuum_pump_failed;
  const double target_suction =
      snapshot_.engine_pump_online ? clamp((engine_rpm - 900.0) / 900.0, 0.0, 1.0) * 5.2 :
      snapshot_.standby_pump_online ? 4.2 :
      0.0;
  snapshot_.suction_inhg = suction_lag_.update(target_suction, dt_s);
  for (auto& consumer : snapshot_.consumers) {
    consumer.powered = consumer.switch_on && !consumer.failed && snapshot_.suction_inhg >= 3.8;
  }
}

const PitotStaticSnapshot& PitotStaticSystem::snapshot() const noexcept {
  return snapshot_;
}

void PitotStaticSystem::reset(double altitude_m, double airspeed_mps) {
  snapshot_ = {};
  snapshot_.atmosphere =
      sample_standard_atmosphere(altitude_m, settings_.qnh_pa, kSeaLevelTemperatureK);
  snapshot_.static_pressure_pa = snapshot_.atmosphere.static_pressure_pa;
  snapshot_.pitot_pressure_pa =
      snapshot_.static_pressure_pa +
      dynamic_pressure_pa(snapshot_.atmosphere.density_kgpm3, airspeed_mps);
  snapshot_.indicated_altitude_m =
      pressure_altitude_from_static_pressure(snapshot_.static_pressure_pa, settings_.qnh_pa);
  snapshot_.indicated_airspeed_mps = airspeed_mps;
  previous_indicated_altitude_m_ = snapshot_.indicated_altitude_m;
  static_lag_.reset(snapshot_.static_pressure_pa);
  pitot_lag_.reset(snapshot_.pitot_pressure_pa);
  vsi_lag_.reset(0.0);
}

void PitotStaticSystem::set_settings(PitotStaticSettings settings) noexcept {
  settings_.qnh_pa = std::max(kMinPressurePa, finite_or(settings.qnh_pa, kSeaLevelPressurePa));
  settings_.qfe_reference_altitude_m = finite_or(settings.qfe_reference_altitude_m, 0.0);
}

void PitotStaticSystem::step(double dt_s,
                             const FlightDynamicsState& truth,
                             AtmosphereSample atmosphere,
                             double icing_severity_norm,
                             bool pitot_heat_powered,
                             const FailureStateModel& failures) {
  require_valid_dt(dt_s);
  const double icing = clamp(finite_or(icing_severity_norm, 0.0), 0.0, 1.0);
  snapshot_.icing_present = icing > 0.05 && !pitot_heat_powered;
  snapshot_.pitot_blocked = failures.pitot_blocked || snapshot_.icing_present;
  snapshot_.static_blocked = failures.static_port_blocked;

  if (atmosphere.static_pressure_pa <= 0.0 || !std::isfinite(atmosphere.static_pressure_pa)) {
    atmosphere = sample_standard_atmosphere(
        truth.altitude_m,
        settings_.qnh_pa,
        atmosphere.temperature_k > 0.0 ? atmosphere.temperature_k : kSeaLevelTemperatureK);
  }
  snapshot_.atmosphere = atmosphere;
  const double speed_mps = std::sqrt(std::max(0.0, dot(truth.body_velocity_mps, truth.body_velocity_mps)));
  const double true_static = snapshot_.atmosphere.static_pressure_pa;
  const double true_pitot =
      true_static + dynamic_pressure_pa(snapshot_.atmosphere.density_kgpm3, speed_mps);

  if (!snapshot_.static_blocked) {
    snapshot_.static_pressure_pa = static_lag_.update(true_static, dt_s);
  }
  if (!snapshot_.pitot_blocked) {
    snapshot_.pitot_pressure_pa = pitot_lag_.update(true_pitot, dt_s);
  }

  const double dynamic_pressure =
      std::max(0.0, snapshot_.pitot_pressure_pa - snapshot_.static_pressure_pa);
  snapshot_.indicated_airspeed_mps =
      std::sqrt(2.0 * dynamic_pressure / std::max(0.1, snapshot_.atmosphere.density_kgpm3));

  const double altimeter_setting =
      settings_.qfe_reference_altitude_m == 0.0
          ? settings_.qnh_pa
          : qfe_pressure_for_field(settings_.qfe_reference_altitude_m, settings_.qnh_pa);
  const double new_altitude =
      pressure_altitude_from_static_pressure(snapshot_.static_pressure_pa, altimeter_setting);
  const double raw_vsi = dt_s > 0.0 ? (new_altitude - previous_indicated_altitude_m_) / dt_s : 0.0;
  snapshot_.vertical_speed_mps = vsi_lag_.update(raw_vsi, dt_s);
  snapshot_.indicated_altitude_m = new_altitude;
  previous_indicated_altitude_m_ = new_altitude;
}

const CompassSnapshot& CompassSystem::snapshot() const noexcept {
  return snapshot_;
}

void CompassSystem::reset(double heading_rad) noexcept {
  snapshot_.magnetic_heading_rad = wrap_radians(heading_rad);
  snapshot_.valid = true;
  heading_lag_.reset(snapshot_.magnetic_heading_rad);
}

void CompassSystem::step(double dt_s, double true_heading_rad, double magnetic_variation_rad) {
  require_valid_dt(dt_s);
  const double target = wrap_radians(true_heading_rad - magnetic_variation_rad);
  const double current = heading_lag_.initialized ? heading_lag_.value : target;
  snapshot_.magnetic_heading_rad =
      wrap_radians(heading_lag_.update(current + shortest_angle_delta(current, target), dt_s));
  snapshot_.valid = true;
}

const GyroSnapshot& GyroSystem::snapshot() const noexcept {
  return snapshot_;
}

void GyroSystem::reset() noexcept {
  snapshot_ = {};
  roll_lag_.reset(0.0);
  pitch_lag_.reset(0.0);
  heading_lag_.reset(0.0);
}

void GyroSystem::step(double dt_s,
                      const FlightDynamicsState& truth,
                      bool attitude_gyro_powered,
                      bool heading_gyro_powered) {
  require_valid_dt(dt_s);
  snapshot_.attitude_valid = attitude_gyro_powered;
  snapshot_.heading_valid = heading_gyro_powered;
  if (attitude_gyro_powered) {
    snapshot_.attitude_roll_rad = roll_lag_.update(truth.euler_rad.x, dt_s);
    snapshot_.attitude_pitch_rad = pitch_lag_.update(truth.euler_rad.y, dt_s);
  }
  if (heading_gyro_powered) {
    const double current = heading_lag_.initialized ? heading_lag_.value : truth.euler_rad.z;
    snapshot_.heading_rad = wrap_radians(
        heading_lag_.update(current + shortest_angle_delta(current, truth.euler_rad.z), dt_s));
  }
}

const GpsSnapshot& GpsSystem::snapshot() const noexcept {
  return snapshot_;
}

void GpsSystem::reset() noexcept {
  snapshot_ = {};
  latitude_lag_.reset(0.0);
  longitude_lag_.reset(0.0);
  altitude_lag_.reset(0.0);
}

void GpsSystem::step(double dt_s,
                     const FlightDynamicsState& truth,
                     bool avionics_powered,
                     const FailureStateModel& failures) {
  require_valid_dt(dt_s);
  snapshot_.valid = avionics_powered && !failures.gps_failed;
  if (!snapshot_.valid) {
    return;
  }
  snapshot_.latitude_deg = latitude_lag_.update(truth.latitude_deg, dt_s);
  snapshot_.longitude_deg = longitude_lag_.update(truth.longitude_deg, dt_s);
  snapshot_.altitude_m = altitude_lag_.update(truth.altitude_m, dt_s);
  snapshot_.ground_speed_mps = horizontal_speed(truth.ned_velocity_mps);
  snapshot_.track_rad = wrap_radians(std::atan2(truth.ned_velocity_mps.y, truth.ned_velocity_mps.x));
}

const EngineInstrumentSnapshot& EngineInstrumentSystem::snapshot() const noexcept {
  return snapshot_;
}

void EngineInstrumentSystem::reset() noexcept {
  snapshot_ = {};
  rpm_lag_.reset(0.0);
  manifold_lag_.reset(kSeaLevelPressurePa / 1'000.0);
  oil_lag_.reset(293.15);
  cht_lag_.reset(293.15);
  egt_lag_.reset(293.15);
  fuel_flow_lag_.reset(0.0);
}

void EngineInstrumentSystem::step(double dt_s,
                                  double commanded_throttle_norm,
                                  double engine_rpm,
                                  double ambient_pressure_pa,
                                  const FuelSystemSnapshot& fuel,
                                  bool sensor_powered,
                                  const FailureStateModel& failures) {
  require_valid_dt(dt_s);
  snapshot_.valid = sensor_powered && !failures.engine_sensor_power_failed;
  snapshot_.fuel_starved = fuel.engine_fuel_starved;
  if (!snapshot_.valid) {
    return;
  }

  const double throttle = clamp(commanded_throttle_norm, 0.0, 1.0);
  const double starvation_factor = fuel.engine_fuel_starved ? 0.35 : 1.0;
  const double sensed_rpm = std::max(0.0, engine_rpm * starvation_factor);
  snapshot_.rpm = rpm_lag_.update(sensed_rpm, dt_s);
  snapshot_.manifold_pressure_kpa =
      manifold_lag_.update((ambient_pressure_pa / 1'000.0) * (0.32 + 0.66 * throttle) *
                               starvation_factor,
                           dt_s);
  snapshot_.fuel_flow_kgps = fuel_flow_lag_.update(fuel.delivered_flow_kgps, dt_s);
  snapshot_.oil_temperature_k =
      oil_lag_.update(293.15 + throttle * 35.0 + snapshot_.rpm / 2'700.0 * 20.0, dt_s);
  snapshot_.cylinder_head_temperature_k =
      cht_lag_.update(300.0 + throttle * 95.0 * starvation_factor, dt_s);
  snapshot_.exhaust_gas_temperature_k =
      egt_lag_.update(550.0 + throttle * 180.0 * starvation_factor, dt_s);
}

AircraftSystemsModel::AircraftSystemsModel() {
  electrical_.set_consumers({
      {"pitot_heat", 90.0},
      {"fuel_pump", 45.0},
      {"standby_vacuum", 60.0},
      {"avionics.gps", 20.0},
      {"engine_instruments", 15.0},
  });
  vacuum_.set_consumers({
      {"attitude_gyro"},
      {"heading_gyro"},
  });
  reset();
}

const InstrumentData& AircraftSystemsModel::instruments() const noexcept {
  return instruments_;
}

ElectricalSystem& AircraftSystemsModel::electrical() noexcept {
  return electrical_;
}

FuelSystem& AircraftSystemsModel::fuel() noexcept {
  return fuel_;
}

VacuumSystem& AircraftSystemsModel::vacuum() noexcept {
  return vacuum_;
}

PitotStaticSystem& AircraftSystemsModel::pitot_static() noexcept {
  return pitot_static_;
}

FailureStateModel& AircraftSystemsModel::failures() noexcept {
  return failures_;
}

AircraftSystemsSwitches& AircraftSystemsModel::switches() noexcept {
  return switches_;
}

void AircraftSystemsModel::reset() {
  instruments_ = {};
  electrical_.reset();
  fuel_.reset();
  vacuum_.reset();
  pitot_static_.reset();
  compass_.reset();
  gyro_.reset();
  gps_.reset();
  engine_.reset();
}

InstrumentData AircraftSystemsModel::step(double dt_s, const AircraftSystemsInput& input) {
  require_valid_dt(dt_s);
  const WeatherSample weather =
      input.weather_valid
          ? input.weather
          : WeatherSample{
                WeatherSource::Manual,
                sample_weather_atmosphere(input.truth.altitude_m,
                                          kSeaLevelPressurePa,
                                          input.outside_air_temperature_k,
                                          0.50)};
  const double weather_power_factor =
      clamp(1.0 - weather.icing_severity_norm * 0.08 - weather.precipitation_rate_mmph * 0.002,
            0.80,
            1.0);
  const double engine_rpm =
      (input.engine_rpm > 0.0 ? input.engine_rpm : 650.0 + input.controls.throttle_norm * 2'050.0) *
      weather_power_factor;

  electrical_.step(dt_s, engine_rpm, switches_, failures_);
  const bool fuel_pump_powered = electrical_.consumer_powered("fuel_pump");
  const bool standby_vacuum_powered = electrical_.consumer_powered("standby_vacuum");
  const bool pitot_heat_powered =
      switches_.pitot_heat_on && electrical_.consumer_powered("pitot_heat") &&
      !failures_.pitot_heat_failed;

  const double requested_fuel_flow = 0.004 + input.controls.throttle_norm * 0.045;
  fuel_.step(dt_s,
             requested_fuel_flow,
             engine_rpm,
             fuel_pump_powered,
             switches_,
             failures_);
  vacuum_.step(dt_s, engine_rpm, standby_vacuum_powered, switches_, failures_);
  pitot_static_.step(dt_s,
                     input.truth,
                     weather.atmosphere,
                     std::max(input.icing_severity_norm, weather.icing_severity_norm),
                     pitot_heat_powered,
                     failures_);
  compass_.step(dt_s, input.truth.euler_rad.z, input.magnetic_variation_rad);
  gyro_.step(dt_s,
             input.truth,
             vacuum_.consumer_powered("attitude_gyro"),
             vacuum_.consumer_powered("heading_gyro"));
  gps_.step(dt_s, input.truth, electrical_.snapshot().avionics_bus_powered, failures_);
  engine_.step(dt_s,
               input.controls.throttle_norm,
               engine_rpm,
               pitot_static_.snapshot().atmosphere.static_pressure_pa,
               fuel_.snapshot(),
               electrical_.consumer_powered("engine_instruments"),
               failures_);

  instruments_.indicated_airspeed_mps = pitot_static_.snapshot().indicated_airspeed_mps;
  instruments_.indicated_altitude_m = pitot_static_.snapshot().indicated_altitude_m;
  instruments_.vertical_speed_mps = pitot_static_.snapshot().vertical_speed_mps;
  instruments_.magnetic_heading_rad = compass_.snapshot().magnetic_heading_rad;
  instruments_.attitude_roll_rad = gyro_.snapshot().attitude_roll_rad;
  instruments_.attitude_pitch_rad = gyro_.snapshot().attitude_pitch_rad;
  instruments_.gyro_heading_rad = gyro_.snapshot().heading_rad;
  instruments_.gps = gps_.snapshot();
  instruments_.engine = engine_.snapshot();
  instruments_.electrical = electrical_.snapshot();
  instruments_.fuel = fuel_.snapshot();
  instruments_.vacuum = vacuum_.snapshot();
  instruments_.pitot_static = pitot_static_.snapshot();
  instruments_.weather = weather;
  ++instruments_.sequence;
  return instruments_;
}

} // namespace flying::core_sim
