#include "flying/core_sim/telemetry.hpp"

#include "flying/core_sim/determinism.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace flying::core_sim {
namespace {

constexpr std::size_t kCurrentInitialStateFieldCount = 64;
constexpr std::size_t kLegacyInitialStateFieldCount = 35;
constexpr std::size_t kCurrentFrameFieldCount = 94;
constexpr std::size_t kLegacyFrameFieldCount = 65;
constexpr std::uint64_t kLegacyFnvOffset = 14'695'981'039'346'656'037ull;
constexpr std::uint64_t kLegacyFnvPrime = 1'099'511'628'211ull;
constexpr std::uint64_t kLegacyHashSchemaVersion = 2;

[[nodiscard]] bool is_initial_state_field_count_supported(std::size_t field_count) noexcept {
  return field_count == kCurrentInitialStateFieldCount ||
         field_count == kLegacyInitialStateFieldCount;
}

[[nodiscard]] bool is_frame_field_count_supported(std::size_t field_count) noexcept {
  return field_count == kCurrentFrameFieldCount ||
         field_count == kLegacyFrameFieldCount;
}

[[nodiscard]] std::uint64_t append_legacy_u64(std::uint64_t hash,
                                              std::uint64_t value) noexcept {
  for (int byte_index = 0; byte_index < 8; ++byte_index) {
    const auto byte = static_cast<std::uint8_t>((value >> (byte_index * 8)) & 0xffu);
    hash ^= byte;
    hash *= kLegacyFnvPrime;
  }
  return hash;
}

[[nodiscard]] std::uint64_t canonical_legacy_double_bits(double value) noexcept {
  if (value == 0.0) {
    value = 0.0;
  }
  if (std::isnan(value)) {
    return 0x7ff8'0000'0000'0000ull;
  }
  return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint64_t append_legacy_double(std::uint64_t hash,
                                                 double value) noexcept {
  return append_legacy_u64(hash, canonical_legacy_double_bits(value));
}

[[nodiscard]] std::uint64_t append_legacy_vector(std::uint64_t hash,
                                                 Vector3d value) noexcept {
  hash = append_legacy_double(hash, value.x);
  hash = append_legacy_double(hash, value.y);
  return append_legacy_double(hash, value.z);
}

[[nodiscard]] std::uint64_t append_legacy_quaternion(std::uint64_t hash,
                                                     Quaterniond value) noexcept {
  hash = append_legacy_double(hash, value.w);
  hash = append_legacy_double(hash, value.x);
  hash = append_legacy_double(hash, value.y);
  return append_legacy_double(hash, value.z);
}

[[nodiscard]] std::uint64_t append_legacy_inertia_tensor(
    std::uint64_t hash,
    AircraftInertiaTensor value) noexcept {
  hash = append_legacy_double(hash, value.ixx);
  hash = append_legacy_double(hash, value.iyy);
  hash = append_legacy_double(hash, value.izz);
  hash = append_legacy_double(hash, value.ixy);
  hash = append_legacy_double(hash, value.ixz);
  return append_legacy_double(hash, value.iyz);
}

[[nodiscard]] std::uint64_t hash_legacy_state(const AuthoritativeState& state) noexcept {
  auto hash = append_legacy_u64(kLegacyFnvOffset, kLegacyHashSchemaVersion);
  hash = append_legacy_double(hash, state.simulation_time_s);
  hash = append_legacy_u64(hash, state.step_index);
  hash = append_legacy_vector(hash, state.ecef_position_m);
  hash = append_legacy_vector(hash, state.ecef_velocity_mps);
  hash = append_legacy_quaternion(hash, state.body_to_ecef);
  hash = append_legacy_vector(hash, state.angular_velocity_body_radps);
  hash = append_legacy_vector(hash, state.accumulated_force_body_n);
  hash = append_legacy_vector(hash, state.accumulated_moment_body_nm);
  hash = append_legacy_double(hash, state.aircraft_mass_balance.total_mass_kg);
  hash = append_legacy_double(hash, state.aircraft_mass_balance.fuel_mass_kg);
  hash = append_legacy_double(hash, state.aircraft_mass_balance.payload_mass_kg);
  hash = append_legacy_vector(hash, state.aircraft_mass_balance.center_of_gravity_body_m);
  return append_legacy_inertia_tensor(hash, state.aircraft_mass_balance.inertia_tensor_kg_m2);
}

#ifndef FLYING_CORE_SIM_VERSION
#define FLYING_CORE_SIM_VERSION "0.1.0"
#endif

[[nodiscard]] bool is_finite(Vector3d value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool is_finite(Quaterniond value) noexcept {
  return std::isfinite(value.w) && std::isfinite(value.x) &&
         std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool is_non_negative_finite(double value) noexcept {
  return value >= 0.0 && std::isfinite(value);
}

[[nodiscard]] bool is_unit_interval(double value) noexcept {
  return value >= 0.0 && value <= 1.0 && std::isfinite(value);
}

[[nodiscard]] bool contains_record_delimiter(std::string_view value) noexcept {
  return value.find('|') != std::string_view::npos ||
         value.find('\n') != std::string_view::npos ||
         value.find('\r') != std::string_view::npos;
}

[[nodiscard]] std::string to_owned(std::string_view value) {
  return std::string(value.begin(), value.end());
}

[[nodiscard]] std::vector<std::string_view> split_fields(std::string_view line) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const std::size_t next = line.find('|', start);
    if (next == std::string_view::npos) {
      fields.push_back(line.substr(start));
      break;
    }

    fields.push_back(line.substr(start, next - start));
    start = next + 1;
  }

  return fields;
}

[[nodiscard]] bool parse_double(std::string_view value, double& parsed) noexcept {
  try {
    std::size_t consumed = 0;
    const std::string owned = to_owned(value);
    parsed = std::stod(owned, &consumed);
    return consumed == owned.size() && std::isfinite(parsed);
  } catch (const std::exception&) {
    return false;
  }
}

[[nodiscard]] bool parse_u64(std::string_view value, std::uint64_t& parsed) noexcept {
  try {
    std::size_t consumed = 0;
    const std::string owned = to_owned(value);
    parsed = std::stoull(owned, &consumed, 10);
    return consumed == owned.size();
  } catch (const std::exception&) {
    return false;
  }
}

[[nodiscard]] bool parse_u32(std::string_view value, std::uint32_t& parsed) noexcept {
  std::uint64_t wider = 0;
  if (!parse_u64(value, wider) || wider > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  parsed = static_cast<std::uint32_t>(wider);
  return true;
}

[[nodiscard]] bool parse_i64(std::string_view value, std::int64_t& parsed) noexcept {
  try {
    std::size_t consumed = 0;
    const std::string owned = to_owned(value);
    parsed = std::stoll(owned, &consumed, 10);
    return consumed == owned.size();
  } catch (const std::exception&) {
    return false;
  }
}

[[nodiscard]] bool parse_bool(std::string_view value, bool& parsed) noexcept {
  if (value == "0") {
    parsed = false;
    return true;
  }
  if (value == "1") {
    parsed = true;
    return true;
  }
  return false;
}

[[nodiscard]] std::string double_to_string(double value) {
  std::ostringstream stream;
  stream.precision(std::numeric_limits<double>::max_digits10);
  stream << value;
  return stream.str();
}

[[nodiscard]] std::string bool_to_string(bool value) {
  return value ? "1" : "0";
}

[[nodiscard]] std::string u64_to_hex(std::uint64_t value) {
  std::ostringstream stream;
  stream << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
  return stream.str();
}

void append_field(std::ostream& output, std::string_view value) {
  output << '|' << value;
}

void append_field(std::ostream& output, double value) {
  output << '|' << double_to_string(value);
}

void append_field(std::ostream& output, std::uint64_t value) {
  output << '|' << value;
}

void append_field(std::ostream& output, std::uint32_t value) {
  output << '|' << value;
}

void append_field(std::ostream& output, std::int64_t value) {
  output << '|' << value;
}

void append_field(std::ostream& output, bool value) {
  output << '|' << bool_to_string(value);
}

void append_vector(std::ostream& output, Vector3d value) {
  append_field(output, value.x);
  append_field(output, value.y);
  append_field(output, value.z);
}

void append_quaternion(std::ostream& output, Quaterniond value) {
  append_field(output, value.w);
  append_field(output, value.x);
  append_field(output, value.y);
  append_field(output, value.z);
}

void append_controls(std::ostream& output, const AircraftControlInputSample& controls) {
  append_field(output, controls.aileron_norm);
  append_field(output, controls.elevator_norm);
  append_field(output, controls.rudder_norm);
  append_field(output, controls.throttle_norm);
  append_field(output, controls.flaps_norm);
  append_field(output, controls.brake_left_norm);
  append_field(output, controls.brake_right_norm);
  append_field(output, controls.mixture_norm);
  append_field(output, controls.propeller_norm);
  append_field(output, controls.elevator_trim_norm);
  append_field(output, controls.aileron_trim_norm);
  append_field(output, controls.rudder_trim_norm);
}

void append_core_input(std::ostream& output, const ControlInputSample& input) {
  append_vector(output, input.force_body_n);
  append_vector(output, input.moment_body_nm);
}

void append_rigid_body_parameters(std::ostream& output, const RigidBodyParameters& parameters) {
  append_field(output, parameters.mass_kg);
  append_vector(output, parameters.inertia_diagonal_kg_m2);
}

void append_inertia_tensor(std::ostream& output, const AircraftInertiaTensor& inertia) {
  append_field(output, inertia.ixx);
  append_field(output, inertia.iyy);
  append_field(output, inertia.izz);
  append_field(output, inertia.ixy);
  append_field(output, inertia.ixz);
  append_field(output, inertia.iyz);
}

void append_aircraft_mass_balance(std::ostream& output,
                                  const AircraftMassBalanceState& mass_balance) {
  append_field(output, mass_balance.total_mass_kg);
  append_field(output, mass_balance.fuel_mass_kg);
  append_field(output, mass_balance.payload_mass_kg);
  append_vector(output, mass_balance.center_of_gravity_body_m);
  append_inertia_tensor(output, mass_balance.inertia_tensor_kg_m2);
  append_field(output, mass_balance.cg_within_envelope);
}

void append_atmosphere(std::ostream& output, const AtmosphereSample& atmosphere) {
  append_field(output, atmosphere.static_pressure_pa);
  append_field(output, atmosphere.temperature_k);
  append_field(output, atmosphere.density_kgpm3);
  append_field(output, atmosphere.speed_of_sound_mps);
  append_field(output, atmosphere.relative_humidity_norm);
}

void append_weather(std::ostream& output, const WeatherSample& weather) {
  append_field(output, static_cast<std::uint64_t>(weather.source));
  append_atmosphere(output, weather.atmosphere);
  append_vector(output, weather.steady_wind_ned_mps);
  append_vector(output, weather.gust_ned_mps);
  append_vector(output, weather.turbulence_ned_mps);
  append_vector(output, weather.wind_ned_mps);
  append_field(output, weather.visibility_m);
  append_field(output, weather.cloud_coverage_norm);
  append_field(output, weather.precipitation_rate_mmph);
  append_field(output, weather.surface_wetness_norm);
  append_field(output, weather.icing_severity_norm);
  append_field(output, weather.runway_friction_scale);
}

void append_engine(std::ostream& output, const EngineStateSample& engine) {
  append_field(output, engine.engine_running);
  append_field(output, engine.throttle_norm);
  append_field(output, engine.mixture_norm);
  append_field(output, engine.propeller_norm);
}

void append_state(std::ostream& output, const AuthoritativeState& state) {
  append_field(output, state.simulation_time_s);
  append_field(output, state.step_index);
  append_vector(output, state.ecef_position_m);
  append_vector(output, state.ecef_velocity_mps);
  append_quaternion(output, state.body_to_ecef);
  append_vector(output, state.angular_velocity_body_radps);
  append_vector(output, state.accumulated_force_body_n);
  append_vector(output, state.accumulated_moment_body_nm);
  append_aircraft_mass_balance(output, state.aircraft_mass_balance);
  append_weather(output, state.weather);
  append_vector(output, state.relative_air_velocity_body_mps);
  append_field(output, state.weather_dynamic_pressure_pa);
}

void append_flight_dynamics_initial_condition(
    std::ostream& output,
    const FlightDynamicsInitialCondition& initial_condition) {
  append_field(output, initial_condition.latitude_deg);
  append_field(output, initial_condition.longitude_deg);
  append_field(output, initial_condition.altitude_m);
  append_field(output, initial_condition.terrain_elevation_m);
  append_vector(output, initial_condition.body_velocity_mps);
  append_vector(output, initial_condition.angular_velocity_body_radps);
  append_field(output, initial_condition.roll_rad);
  append_field(output, initial_condition.pitch_rad);
  append_field(output, initial_condition.heading_rad);
}

[[nodiscard]] bool read_double(const std::vector<std::string_view>& fields,
                               std::size_t& index,
                               double& value) noexcept {
  if (index >= fields.size()) {
    return false;
  }
  return parse_double(fields[index++], value);
}

[[nodiscard]] bool read_u64(const std::vector<std::string_view>& fields,
                            std::size_t& index,
                            std::uint64_t& value) noexcept {
  if (index >= fields.size()) {
    return false;
  }
  return parse_u64(fields[index++], value);
}

[[nodiscard]] bool read_u32(const std::vector<std::string_view>& fields,
                            std::size_t& index,
                            std::uint32_t& value) noexcept {
  if (index >= fields.size()) {
    return false;
  }
  return parse_u32(fields[index++], value);
}

[[nodiscard]] bool read_i64(const std::vector<std::string_view>& fields,
                            std::size_t& index,
                            std::int64_t& value) noexcept {
  if (index >= fields.size()) {
    return false;
  }
  return parse_i64(fields[index++], value);
}

[[nodiscard]] bool read_bool(const std::vector<std::string_view>& fields,
                             std::size_t& index,
                             bool& value) noexcept {
  if (index >= fields.size()) {
    return false;
  }
  return parse_bool(fields[index++], value);
}

[[nodiscard]] bool read_vector(const std::vector<std::string_view>& fields,
                               std::size_t& index,
                               Vector3d& value) noexcept {
  return read_double(fields, index, value.x) &&
         read_double(fields, index, value.y) &&
         read_double(fields, index, value.z);
}

[[nodiscard]] bool read_quaternion(const std::vector<std::string_view>& fields,
                                   std::size_t& index,
                                   Quaterniond& value) noexcept {
  return read_double(fields, index, value.w) &&
         read_double(fields, index, value.x) &&
         read_double(fields, index, value.y) &&
         read_double(fields, index, value.z);
}

[[nodiscard]] bool read_controls(const std::vector<std::string_view>& fields,
                                 std::size_t& index,
                                 AircraftControlInputSample& controls) noexcept {
  return read_double(fields, index, controls.aileron_norm) &&
         read_double(fields, index, controls.elevator_norm) &&
         read_double(fields, index, controls.rudder_norm) &&
         read_double(fields, index, controls.throttle_norm) &&
         read_double(fields, index, controls.flaps_norm) &&
         read_double(fields, index, controls.brake_left_norm) &&
         read_double(fields, index, controls.brake_right_norm) &&
         read_double(fields, index, controls.mixture_norm) &&
         read_double(fields, index, controls.propeller_norm) &&
         read_double(fields, index, controls.elevator_trim_norm) &&
         read_double(fields, index, controls.aileron_trim_norm) &&
         read_double(fields, index, controls.rudder_trim_norm);
}

[[nodiscard]] bool read_core_input(const std::vector<std::string_view>& fields,
                                   std::size_t& index,
                                   ControlInputSample& input) noexcept {
  return read_vector(fields, index, input.force_body_n) &&
         read_vector(fields, index, input.moment_body_nm);
}

[[nodiscard]] bool read_rigid_body_parameters(const std::vector<std::string_view>& fields,
                                              std::size_t& index,
                                              RigidBodyParameters& parameters) noexcept {
  return read_double(fields, index, parameters.mass_kg) &&
         read_vector(fields, index, parameters.inertia_diagonal_kg_m2);
}

[[nodiscard]] bool read_inertia_tensor(const std::vector<std::string_view>& fields,
                                       std::size_t& index,
                                       AircraftInertiaTensor& inertia) noexcept {
  return read_double(fields, index, inertia.ixx) &&
         read_double(fields, index, inertia.iyy) &&
         read_double(fields, index, inertia.izz) &&
         read_double(fields, index, inertia.ixy) &&
         read_double(fields, index, inertia.ixz) &&
         read_double(fields, index, inertia.iyz);
}

[[nodiscard]] bool read_aircraft_mass_balance(const std::vector<std::string_view>& fields,
                                              std::size_t& index,
                                              AircraftMassBalanceState& mass_balance) noexcept {
  return read_double(fields, index, mass_balance.total_mass_kg) &&
         read_double(fields, index, mass_balance.fuel_mass_kg) &&
         read_double(fields, index, mass_balance.payload_mass_kg) &&
         read_vector(fields, index, mass_balance.center_of_gravity_body_m) &&
         read_inertia_tensor(fields, index, mass_balance.inertia_tensor_kg_m2) &&
         read_bool(fields, index, mass_balance.cg_within_envelope);
}

[[nodiscard]] bool read_atmosphere(const std::vector<std::string_view>& fields,
                                   std::size_t& index,
                                   AtmosphereSample& atmosphere) noexcept {
  return read_double(fields, index, atmosphere.static_pressure_pa) &&
         read_double(fields, index, atmosphere.temperature_k) &&
         read_double(fields, index, atmosphere.density_kgpm3) &&
         read_double(fields, index, atmosphere.speed_of_sound_mps) &&
         read_double(fields, index, atmosphere.relative_humidity_norm);
}

[[nodiscard]] bool read_weather(const std::vector<std::string_view>& fields,
                                std::size_t& index,
                                WeatherSample& weather) noexcept {
  std::uint64_t source = 0;
  if (!read_u64(fields, index, source) || source > static_cast<std::uint64_t>(WeatherSource::Unavailable)) {
    return false;
  }
  weather.source = static_cast<WeatherSource>(source);
  return read_atmosphere(fields, index, weather.atmosphere) &&
         read_vector(fields, index, weather.steady_wind_ned_mps) &&
         read_vector(fields, index, weather.gust_ned_mps) &&
         read_vector(fields, index, weather.turbulence_ned_mps) &&
         read_vector(fields, index, weather.wind_ned_mps) &&
         read_double(fields, index, weather.visibility_m) &&
         read_double(fields, index, weather.cloud_coverage_norm) &&
         read_double(fields, index, weather.precipitation_rate_mmph) &&
         read_double(fields, index, weather.surface_wetness_norm) &&
         read_double(fields, index, weather.icing_severity_norm) &&
         read_double(fields, index, weather.runway_friction_scale);
}

[[nodiscard]] bool read_engine(const std::vector<std::string_view>& fields,
                               std::size_t& index,
                               EngineStateSample& engine) noexcept {
  return read_bool(fields, index, engine.engine_running) &&
         read_double(fields, index, engine.throttle_norm) &&
         read_double(fields, index, engine.mixture_norm) &&
         read_double(fields, index, engine.propeller_norm);
}

[[nodiscard]] bool read_state(const std::vector<std::string_view>& fields,
                              std::size_t& index,
                              AuthoritativeState& state) noexcept {
  return read_double(fields, index, state.simulation_time_s) &&
         read_u64(fields, index, state.step_index) &&
         read_vector(fields, index, state.ecef_position_m) &&
         read_vector(fields, index, state.ecef_velocity_mps) &&
         read_quaternion(fields, index, state.body_to_ecef) &&
         read_vector(fields, index, state.angular_velocity_body_radps) &&
         read_vector(fields, index, state.accumulated_force_body_n) &&
         read_vector(fields, index, state.accumulated_moment_body_nm) &&
         read_aircraft_mass_balance(fields, index, state.aircraft_mass_balance) &&
         read_weather(fields, index, state.weather) &&
         read_vector(fields, index, state.relative_air_velocity_body_mps) &&
         read_double(fields, index, state.weather_dynamic_pressure_pa);
}

[[nodiscard]] bool read_legacy_state(const std::vector<std::string_view>& fields,
                                     std::size_t& index,
                                     AuthoritativeState& state) noexcept {
  state = {};
  if (!read_double(fields, index, state.simulation_time_s) ||
      !read_u64(fields, index, state.step_index) ||
      !read_vector(fields, index, state.ecef_position_m) ||
      !read_vector(fields, index, state.ecef_velocity_mps) ||
      !read_quaternion(fields, index, state.body_to_ecef) ||
      !read_vector(fields, index, state.angular_velocity_body_radps) ||
      !read_vector(fields, index, state.accumulated_force_body_n) ||
      !read_vector(fields, index, state.accumulated_moment_body_nm) ||
      !read_double(fields, index, state.aircraft_mass_balance.total_mass_kg) ||
      !read_double(fields, index, state.aircraft_mass_balance.fuel_mass_kg) ||
      !read_double(fields, index, state.aircraft_mass_balance.payload_mass_kg) ||
      !read_vector(fields, index, state.aircraft_mass_balance.center_of_gravity_body_m) ||
      !read_inertia_tensor(fields, index, state.aircraft_mass_balance.inertia_tensor_kg_m2)) {
    return false;
  }
  state.aircraft_mass_balance.cg_within_envelope = true;
  return true;
}

[[nodiscard]] bool read_flight_dynamics_initial_condition(
    const std::vector<std::string_view>& fields,
    std::size_t& index,
    FlightDynamicsInitialCondition& initial_condition) noexcept {
  return read_double(fields, index, initial_condition.latitude_deg) &&
         read_double(fields, index, initial_condition.longitude_deg) &&
         read_double(fields, index, initial_condition.altitude_m) &&
         read_double(fields, index, initial_condition.terrain_elevation_m) &&
         read_vector(fields, index, initial_condition.body_velocity_mps) &&
         read_vector(fields, index, initial_condition.angular_velocity_body_radps) &&
         read_double(fields, index, initial_condition.roll_rad) &&
         read_double(fields, index, initial_condition.pitch_rad) &&
         read_double(fields, index, initial_condition.heading_rad);
}

[[nodiscard]] bool valid_state(const AuthoritativeState& state) noexcept {
  const AircraftInertiaTensor& inertia = state.aircraft_mass_balance.inertia_tensor_kg_m2;
  return std::isfinite(state.simulation_time_s) &&
         is_finite(state.ecef_position_m) &&
         is_finite(state.ecef_velocity_mps) &&
         is_finite(state.body_to_ecef) &&
         is_finite(state.angular_velocity_body_radps) &&
         is_finite(state.accumulated_force_body_n) &&
         is_finite(state.accumulated_moment_body_nm) &&
         state.aircraft_mass_balance.total_mass_kg > 0.0 &&
         std::isfinite(state.aircraft_mass_balance.total_mass_kg) &&
         state.aircraft_mass_balance.fuel_mass_kg >= 0.0 &&
         std::isfinite(state.aircraft_mass_balance.fuel_mass_kg) &&
         state.aircraft_mass_balance.payload_mass_kg >= 0.0 &&
         std::isfinite(state.aircraft_mass_balance.payload_mass_kg) &&
         is_finite(state.aircraft_mass_balance.center_of_gravity_body_m) &&
         inertia.ixx > 0.0 &&
         inertia.iyy > 0.0 &&
         inertia.izz > 0.0 &&
         std::isfinite(inertia.ixx) &&
         std::isfinite(inertia.iyy) &&
         std::isfinite(inertia.izz) &&
         std::isfinite(inertia.ixy) &&
         std::isfinite(inertia.ixz) &&
         std::isfinite(inertia.iyz);
}

[[nodiscard]] bool valid_core_input(const ControlInputSample& input) noexcept {
  return is_finite(input.force_body_n) && is_finite(input.moment_body_nm);
}

[[nodiscard]] bool valid_rigid_body_parameters(const RigidBodyParameters& parameters) noexcept {
  return parameters.mass_kg > 0.0 &&
         std::isfinite(parameters.mass_kg) &&
         parameters.inertia_diagonal_kg_m2.x > 0.0 &&
         parameters.inertia_diagonal_kg_m2.y > 0.0 &&
         parameters.inertia_diagonal_kg_m2.z > 0.0 &&
         is_finite(parameters.inertia_diagonal_kg_m2);
}

[[nodiscard]] bool same_rigid_body_parameters(const RigidBodyParameters& recorded,
                                              const RigidBodyParameters& runtime) noexcept {
  return recorded.mass_kg == runtime.mass_kg &&
         recorded.inertia_diagonal_kg_m2.x == runtime.inertia_diagonal_kg_m2.x &&
         recorded.inertia_diagonal_kg_m2.y == runtime.inertia_diagonal_kg_m2.y &&
         recorded.inertia_diagonal_kg_m2.z == runtime.inertia_diagonal_kg_m2.z;
}

void append_string_error(std::vector<std::string>& errors,
                         std::string_view field_name,
                         std::string_view value) {
  if (contains_record_delimiter(value)) {
    errors.push_back(std::string(field_name) +
                     " must not contain '|', newline, or carriage return");
  }
}

void append_aircraft_string_errors(std::vector<std::string>& errors,
                                   const AircraftConfigurationIdentity& aircraft,
                                   std::string_view prefix) {
  append_string_error(errors, std::string(prefix) + ".backend", aircraft.backend);
  append_string_error(errors, std::string(prefix) + ".model_name", aircraft.model_name);
  append_string_error(errors, std::string(prefix) + ".model_version", aircraft.model_version);
  append_string_error(errors, std::string(prefix) + ".data_root", aircraft.data_root);
  append_string_error(errors, std::string(prefix) + ".source_license", aircraft.source_license);
}

[[nodiscard]] bool same_aircraft(const AircraftConfigurationIdentity& recorded,
                                 const AircraftConfigurationIdentity& runtime) noexcept {
  return recorded.backend == runtime.backend &&
         recorded.model_name == runtime.model_name &&
         recorded.model_version == runtime.model_version;
}

[[nodiscard]] const DataPackageVersion* find_package(
    std::string_view package_id,
    const std::vector<DataPackageVersion>& packages) noexcept {
  const auto found = std::find_if(
      packages.begin(),
      packages.end(),
      [&](const DataPackageVersion& package) {
        return package.package_id == package_id;
      });
  return found == packages.end() ? nullptr : &*found;
}

void append_json_string(std::ostream& output, std::string_view value) {
  output << '"';
  for (const unsigned char byte : value) {
    switch (byte) {
    case '"':
      output << "\\\"";
      break;
    case '\\':
      output << "\\\\";
      break;
    case '\b':
      output << "\\b";
      break;
    case '\f':
      output << "\\f";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      if (byte < 0x20) {
        output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
               << static_cast<int>(byte) << std::dec << std::setfill(' ');
      } else {
        output << static_cast<char>(byte);
      }
      break;
    }
  }
  output << '"';
}

void append_csv_string(std::ostream& output, std::string_view value) {
  output << '"';
  const auto first_non_space = std::find_if(
      value.begin(),
      value.end(),
      [](char ch) {
        return ch != ' ' && ch != '\t';
      });
  if (first_non_space != value.end() &&
      (*first_non_space == '=' ||
       *first_non_space == '+' ||
       *first_non_space == '-' ||
       *first_non_space == '@')) {
    output << '\'';
  }
  for (const char ch : value) {
    if (ch == '"') {
      output << "\"\"";
    } else {
      output << ch;
    }
  }
  output << '"';
}

void append_csv_metadata_string(std::ostream& output,
                                std::string_view field_name,
                                std::string_view value) {
  output << "# " << field_name << ',';
  append_csv_string(output, value);
  output << '\n';
}

void append_json_vector(std::ostream& output, Vector3d value) {
  output << "{\"x\":" << double_to_string(value.x)
         << ",\"y\":" << double_to_string(value.y)
         << ",\"z\":" << double_to_string(value.z) << '}';
}

void append_json_quaternion(std::ostream& output, Quaterniond value) {
  output << "{\"w\":" << double_to_string(value.w)
         << ",\"x\":" << double_to_string(value.x)
         << ",\"y\":" << double_to_string(value.y)
         << ",\"z\":" << double_to_string(value.z) << '}';
}

void append_json_inertia_tensor(std::ostream& output, const AircraftInertiaTensor& inertia) {
  output << "{\"ixx\":" << double_to_string(inertia.ixx)
         << ",\"iyy\":" << double_to_string(inertia.iyy)
         << ",\"izz\":" << double_to_string(inertia.izz)
         << ",\"ixy\":" << double_to_string(inertia.ixy)
         << ",\"ixz\":" << double_to_string(inertia.ixz)
         << ",\"iyz\":" << double_to_string(inertia.iyz) << '}';
}

void append_json_aircraft_mass_balance(std::ostream& output,
                                       const AircraftMassBalanceState& mass_balance) {
  output << "{\"total_mass_kg\":" << double_to_string(mass_balance.total_mass_kg)
         << ",\"fuel_mass_kg\":" << double_to_string(mass_balance.fuel_mass_kg)
         << ",\"payload_mass_kg\":" << double_to_string(mass_balance.payload_mass_kg)
         << ",\"center_of_gravity_body_m\":";
  append_json_vector(output, mass_balance.center_of_gravity_body_m);
  output << ",\"inertia_tensor_kg_m2\":";
  append_json_inertia_tensor(output, mass_balance.inertia_tensor_kg_m2);
  output << ",\"cg_within_envelope\":"
         << (mass_balance.cg_within_envelope ? "true" : "false") << '}';
}

void append_json_aircraft_controls(std::ostream& output,
                                   const AircraftControlInputSample& controls) {
  output << "{\"aileron_norm\":" << double_to_string(controls.aileron_norm)
         << ",\"elevator_norm\":" << double_to_string(controls.elevator_norm)
         << ",\"rudder_norm\":" << double_to_string(controls.rudder_norm)
         << ",\"throttle_norm\":" << double_to_string(controls.throttle_norm)
         << ",\"flaps_norm\":" << double_to_string(controls.flaps_norm)
         << ",\"brake_left_norm\":" << double_to_string(controls.brake_left_norm)
         << ",\"brake_right_norm\":" << double_to_string(controls.brake_right_norm)
         << ",\"mixture_norm\":" << double_to_string(controls.mixture_norm)
         << ",\"propeller_norm\":" << double_to_string(controls.propeller_norm)
         << ",\"elevator_trim_norm\":" << double_to_string(controls.elevator_trim_norm)
         << ",\"aileron_trim_norm\":" << double_to_string(controls.aileron_trim_norm)
         << ",\"rudder_trim_norm\":" << double_to_string(controls.rudder_trim_norm) << '}';
}

void append_json_core_input(std::ostream& output, const ControlInputSample& input) {
  output << "{\"force_body_n\":";
  append_json_vector(output, input.force_body_n);
  output << ",\"moment_body_nm\":";
  append_json_vector(output, input.moment_body_nm);
  output << '}';
}

void append_json_engine(std::ostream& output, const EngineStateSample& engine) {
  output << "{\"engine_running\":" << (engine.engine_running ? "true" : "false")
         << ",\"throttle_norm\":" << double_to_string(engine.throttle_norm)
         << ",\"mixture_norm\":" << double_to_string(engine.mixture_norm)
         << ",\"propeller_norm\":" << double_to_string(engine.propeller_norm) << '}';
}

void append_json_state(std::ostream& output, const AuthoritativeState& state) {
  output << "{\"simulation_time_s\":" << double_to_string(state.simulation_time_s)
         << ",\"step_index\":" << state.step_index
         << ",\"ecef_position_m\":";
  append_json_vector(output, state.ecef_position_m);
  output << ",\"ecef_velocity_mps\":";
  append_json_vector(output, state.ecef_velocity_mps);
  output << ",\"body_to_ecef\":";
  append_json_quaternion(output, state.body_to_ecef);
  output << ",\"angular_velocity_body_radps\":";
  append_json_vector(output, state.angular_velocity_body_radps);
  output << ",\"accumulated_force_body_n\":";
  append_json_vector(output, state.accumulated_force_body_n);
  output << ",\"accumulated_moment_body_nm\":";
  append_json_vector(output, state.accumulated_moment_body_nm);
  output << ",\"aircraft_mass_balance\":";
  append_json_aircraft_mass_balance(output, state.aircraft_mass_balance);
  output << '}';
}

void write_recording(std::ostream& output, const TelemetryRecording& recording) {
  output << kTelemetrySchemaVersion << '\n';

  const TelemetryMetadata& metadata = recording.metadata;
  output << "metadata";
  append_field(output, metadata.core_sim_version);
  append_field(output, metadata.aircraft.backend);
  append_field(output, metadata.aircraft.model_name);
  append_field(output, metadata.aircraft.model_version);
  append_field(output, metadata.aircraft.data_root);
  append_field(output, metadata.aircraft.source_license);
  append_field(output, metadata.simulation_configuration_id);
  append_field(output, metadata.input_profile_id);
  append_field(output, metadata.scenario_location_id);
  append_field(output, metadata.scenario_start_mode);
  append_field(output, metadata.session_id);
  append_field(output, metadata.started_unix_ms);
  append_field(output, metadata.fixed_step_s);
  append_field(output, metadata.deterministic_tolerance.simulation_time_s);
  append_field(output, metadata.deterministic_tolerance.position_m);
  append_field(output, metadata.deterministic_tolerance.velocity_mps);
  append_field(output, metadata.deterministic_tolerance.quaternion_component);
  append_field(output, metadata.deterministic_tolerance.angular_velocity_radps);
  append_field(output, metadata.deterministic_tolerance.force_n);
  append_field(output, metadata.deterministic_tolerance.moment_nm);
  output << '\n';

  for (const DataPackageVersion& package : metadata.data_packages) {
    output << "data_package";
    append_field(output, package.package_id);
    append_field(output, package.version);
    output << '\n';
  }

  output << "rigid_body_parameters";
  append_rigid_body_parameters(output, recording.rigid_body_parameters);
  output << '\n';

  output << "initial_state";
  append_state(output, recording.initial_state);
  append_field(output, hash_state(recording.initial_state));
  output << '\n';

  output << "initial_flight_dynamics";
  append_flight_dynamics_initial_condition(output, recording.initial_flight_dynamics);
  output << '\n';

  output << "initial_aircraft_controls";
  append_controls(output, recording.initial_aircraft_controls);
  output << '\n';

  for (const TelemetryFrame& frame : recording.frames) {
    output << "frame";
    append_field(output, frame.frame_index);
    append_field(output, frame.host_time_unix_ms);
    append_field(output, frame.caller_delta_s);
    append_field(output, frame.steps_executed);
    append_field(output, frame.fixed_step_s);
    append_field(output, frame.consumed_time_s);
    append_field(output, frame.remaining_accumulator_s);
    append_field(output, frame.total_steps);
    append_core_input(output, frame.core_input);
    append_controls(output, frame.aircraft_controls);
    append_engine(output, frame.engine);
    append_state(output, frame.state);
    append_field(output, frame.state_hash);
    output << '\n';
  }
}

[[nodiscard]] std::filesystem::path temp_path_for(const std::filesystem::path& path) {
  static std::atomic_uint64_t sequence{0};

#ifdef _WIN32
  const auto process_id = static_cast<unsigned long long>(GetCurrentProcessId());
#else
  const auto process_id = static_cast<unsigned long long>(getpid());
#endif

  std::filesystem::path temp = path;
  temp += ".tmp.";
  temp += std::to_string(current_unix_time_ms());
  temp += ".";
  temp += std::to_string(process_id);
  temp += ".";
  temp += std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
  return temp;
}

void remove_temp_file(const std::filesystem::path& temp) noexcept {
  if (temp.empty()) {
    return;
  }

  std::error_code error;
  std::filesystem::remove(temp, error);
}

[[nodiscard]] bool replace_file_atomically(const std::filesystem::path& temp,
                                           const std::filesystem::path& target,
                                           std::error_code& error) noexcept {
#ifdef _WIN32
  if (!MoveFileExW(temp.c_str(),
                   target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
  }
  error.clear();
  return true;
#else
  std::filesystem::rename(temp, target, error);
  return !error;
#endif
}

[[nodiscard]] TelemetryWriteResult write_text_file_atomic(
    const std::filesystem::path& path,
    const std::string& content,
    std::string_view artifact_name) {
  TelemetryWriteResult result{};
  std::filesystem::path temp;

  try {
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
      std::error_code create_error;
      std::filesystem::create_directories(parent, create_error);
      if (create_error) {
        result.errors.push_back("unable to create " + std::string(artifact_name) +
                                " directory: " + create_error.message());
        return result;
      }
    }

    temp = temp_path_for(path);
    {
      std::ofstream output(temp, std::ios::trunc);
      if (!output) {
        remove_temp_file(temp);
        result.errors.push_back("unable to open temporary " + std::string(artifact_name) + " file");
        return result;
      }

      output << content;
      output.flush();
      if (!output) {
        remove_temp_file(temp);
        result.errors.push_back("failed while writing temporary " + std::string(artifact_name) + " file");
        return result;
      }
    }

    std::error_code replace_error;
    if (!replace_file_atomically(temp, path, replace_error)) {
      remove_temp_file(temp);
      result.errors.push_back("unable to replace " + std::string(artifact_name) +
                              " file: " + replace_error.message());
      return result;
    }

    result.saved = true;
    return result;
  } catch (const std::exception& error) {
    remove_temp_file(temp);
    result.errors.push_back(error.what());
    return result;
  }
}

[[nodiscard]] TelemetryExportResult to_export_result(TelemetryWriteResult result) {
  TelemetryExportResult export_result{};
  export_result.exported = result.saved;
  export_result.errors = std::move(result.errors);
  return export_result;
}

[[nodiscard]] bool compare_component(double expected,
                                     double actual,
                                     double tolerance,
                                     double& max_error) noexcept {
  const double error = std::abs(expected - actual);
  max_error = std::max(max_error, error);
  return error <= tolerance;
}

[[nodiscard]] bool compare_vector(Vector3d expected,
                                  Vector3d actual,
                                  double tolerance,
                                  double& max_error) noexcept {
  return compare_component(expected.x, actual.x, tolerance, max_error) &&
         compare_component(expected.y, actual.y, tolerance, max_error) &&
         compare_component(expected.z, actual.z, tolerance, max_error);
}

[[nodiscard]] bool compare_quaternion(Quaterniond expected,
                                      Quaterniond actual,
                                      double tolerance,
                                      double& max_error) noexcept {
  return compare_component(expected.w, actual.w, tolerance, max_error) &&
         compare_component(expected.x, actual.x, tolerance, max_error) &&
         compare_component(expected.y, actual.y, tolerance, max_error) &&
         compare_component(expected.z, actual.z, tolerance, max_error);
}

[[nodiscard]] bool state_within_tolerance(const AuthoritativeState& expected,
                                          const AuthoritativeState& actual,
                                          const DeterminismTolerance& tolerance,
                                          double& max_error) noexcept {
  max_error = 0.0;
  bool within_tolerance = true;
  within_tolerance = compare_component(
      expected.simulation_time_s,
      actual.simulation_time_s,
      tolerance.simulation_time_s,
      max_error) && within_tolerance;
  within_tolerance = expected.step_index == actual.step_index && within_tolerance;
  if (expected.step_index != actual.step_index) {
    max_error = std::max(max_error, static_cast<double>(
        expected.step_index > actual.step_index
            ? expected.step_index - actual.step_index
            : actual.step_index - expected.step_index));
  }
  within_tolerance = compare_vector(
      expected.ecef_position_m,
      actual.ecef_position_m,
      tolerance.position_m,
      max_error) && within_tolerance;
  within_tolerance = compare_vector(
      expected.ecef_velocity_mps,
      actual.ecef_velocity_mps,
      tolerance.velocity_mps,
      max_error) && within_tolerance;
  within_tolerance = compare_quaternion(
      expected.body_to_ecef,
      actual.body_to_ecef,
      tolerance.quaternion_component,
      max_error) && within_tolerance;
  within_tolerance = compare_vector(
      expected.angular_velocity_body_radps,
      actual.angular_velocity_body_radps,
      tolerance.angular_velocity_radps,
      max_error) && within_tolerance;
  within_tolerance = compare_vector(
      expected.accumulated_force_body_n,
      actual.accumulated_force_body_n,
      tolerance.force_n,
      max_error) && within_tolerance;
  within_tolerance = compare_vector(
      expected.accumulated_moment_body_nm,
      actual.accumulated_moment_body_nm,
      tolerance.moment_nm,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.total_mass_kg,
      actual.aircraft_mass_balance.total_mass_kg,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.fuel_mass_kg,
      actual.aircraft_mass_balance.fuel_mass_kg,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.payload_mass_kg,
      actual.aircraft_mass_balance.payload_mass_kg,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_vector(
      expected.aircraft_mass_balance.center_of_gravity_body_m,
      actual.aircraft_mass_balance.center_of_gravity_body_m,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.inertia_tensor_kg_m2.ixx,
      actual.aircraft_mass_balance.inertia_tensor_kg_m2.ixx,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.inertia_tensor_kg_m2.iyy,
      actual.aircraft_mass_balance.inertia_tensor_kg_m2.iyy,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.inertia_tensor_kg_m2.izz,
      actual.aircraft_mass_balance.inertia_tensor_kg_m2.izz,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.inertia_tensor_kg_m2.ixy,
      actual.aircraft_mass_balance.inertia_tensor_kg_m2.ixy,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.inertia_tensor_kg_m2.ixz,
      actual.aircraft_mass_balance.inertia_tensor_kg_m2.ixz,
      0.0,
      max_error) && within_tolerance;
  within_tolerance = compare_component(
      expected.aircraft_mass_balance.inertia_tensor_kg_m2.iyz,
      actual.aircraft_mass_balance.inertia_tensor_kg_m2.iyz,
      0.0,
      max_error) && within_tolerance;
  within_tolerance =
      expected.aircraft_mass_balance.cg_within_envelope ==
          actual.aircraft_mass_balance.cg_within_envelope &&
      within_tolerance;
  return within_tolerance;
}

} // namespace

std::string_view core_sim_version() noexcept {
  return FLYING_CORE_SIM_VERSION;
}

std::int64_t current_unix_time_ms() noexcept {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

TelemetryRecorder::TelemetryRecorder(TelemetryMetadata metadata,
                                     AuthoritativeState initial_state,
                                     FlightDynamicsInitialCondition initial_flight_dynamics,
                                     AircraftControlInputSample initial_aircraft_controls,
                                     RigidBodyParameters rigid_body_parameters) {
  if (metadata.started_unix_ms == 0) {
    metadata.started_unix_ms = current_unix_time_ms();
  }
  if (metadata.core_sim_version.empty()) {
    metadata.core_sim_version = std::string(core_sim_version());
  }
  if (metadata.schema_version.empty()) {
    metadata.schema_version = std::string(kTelemetrySchemaVersion);
  }

  recording_.metadata = std::move(metadata);
  recording_.rigid_body_parameters = rigid_body_parameters;
  recording_.initial_state = initial_state;
  recording_.initial_flight_dynamics = initial_flight_dynamics;
  recording_.initial_aircraft_controls = initial_aircraft_controls;
}

const TelemetryRecording& TelemetryRecorder::recording() const noexcept {
  return recording_;
}

TelemetryRecording& TelemetryRecorder::recording() noexcept {
  return recording_;
}

const TelemetryFrame& TelemetryRecorder::record_advance(
    double caller_delta_s,
    const ControlInputSample& core_input,
    const AdvanceReport& report,
    const AuthoritativeState& state) {
  const EngineStateSample engine = make_engine_state_sample(
      recording_.initial_aircraft_controls,
      recording_.initial_aircraft_controls.mixture_norm > 0.0);
  return record_advance(
      caller_delta_s,
      core_input,
      recording_.initial_aircraft_controls,
      engine,
      report,
      state,
      current_unix_time_ms());
}

const TelemetryFrame& TelemetryRecorder::record_advance(
    double caller_delta_s,
    const ControlInputSample& core_input,
    const AircraftControlInputSample& aircraft_controls,
    const EngineStateSample& engine,
    const AdvanceReport& report,
    const AuthoritativeState& state,
    std::int64_t host_time_unix_ms) {
  TelemetryFrame frame{};
  frame.frame_index = recording_.frames.size();
  frame.host_time_unix_ms = host_time_unix_ms;
  frame.caller_delta_s = caller_delta_s;
  frame.steps_executed = report.steps_executed;
  frame.fixed_step_s = report.fixed_step_s;
  frame.consumed_time_s = report.consumed_time_s;
  frame.remaining_accumulator_s = report.remaining_accumulator_s;
  frame.total_steps = report.total_steps;
  frame.core_input = core_input;
  frame.aircraft_controls = aircraft_controls;
  frame.engine = engine;
  frame.state = state;
  frame.state_hash = report.state_hash != 0 ? report.state_hash : hash_state(state);
  recording_.frames.push_back(frame);
  return recording_.frames.back();
}

TelemetryMetadata make_default_telemetry_metadata() {
  TelemetryMetadata metadata{};
  metadata.schema_version = std::string(kTelemetrySchemaVersion);
  metadata.core_sim_version = std::string(core_sim_version());
  metadata.aircraft = {
    "core_sim",
    "synthetic_rigid_body",
    "v1",
    "core_sim",
    "Flying repository fixtures",
  };
  metadata.simulation_configuration_id = "core_sim.fixed_step.synthetic.v1";
  metadata.input_profile_id = "core_sim.direct_force_moment.v1";
  metadata.session_id = "core_sim-session";
  metadata.fixed_step_s = kFixedStepSeconds;
  metadata.started_unix_ms = current_unix_time_ms();
  metadata.data_packages = {
    {"core_sim.synthetic_rigid_body", "v1"},
  };
  return metadata;
}

EngineStateSample make_engine_state_sample(const AircraftControlInputSample& controls,
                                           bool engine_running) noexcept {
  return {
    engine_running,
    controls.throttle_norm,
    controls.mixture_norm,
    controls.propeller_norm,
  };
}

ReplayEnvironment make_current_replay_environment() {
  const TelemetryMetadata metadata = make_default_telemetry_metadata();
  ReplayEnvironment environment{};
  environment.telemetry_schema_version = std::string(kTelemetrySchemaVersion);
  environment.core_sim_version = std::string(core_sim_version());
  environment.aircraft = metadata.aircraft;
  environment.rigid_body_parameters = {};
  environment.data_packages = metadata.data_packages;
  return environment;
}

std::vector<std::string> validate_telemetry_recording(const TelemetryRecording& recording) {
  std::vector<std::string> errors;
  const TelemetryMetadata& metadata = recording.metadata;

  if (metadata.schema_version != kTelemetrySchemaVersion) {
    errors.push_back("telemetry schema_version must be flying.telemetry.v1");
  }
  if (metadata.core_sim_version.empty()) {
    errors.push_back("core_sim_version must be recorded");
  }
  if (metadata.aircraft.backend.empty() ||
      metadata.aircraft.model_name.empty() ||
      metadata.aircraft.model_version.empty()) {
    errors.push_back("aircraft backend, model_name, and model_version must be recorded");
  }
  if (metadata.simulation_configuration_id.empty()) {
    errors.push_back("simulation_configuration_id must be recorded");
  }
  if (metadata.input_profile_id.empty()) {
    errors.push_back("input_profile_id must be recorded");
  }
  if (!std::isfinite(metadata.fixed_step_s) || metadata.fixed_step_s <= 0.0) {
    errors.push_back("metadata fixed_step_s must be positive and finite");
  }

  append_string_error(errors, "metadata.core_sim_version", metadata.core_sim_version);
  append_aircraft_string_errors(errors, metadata.aircraft, "metadata.aircraft");
  append_string_error(errors, "metadata.simulation_configuration_id",
                      metadata.simulation_configuration_id);
  append_string_error(errors, "metadata.input_profile_id", metadata.input_profile_id);
  append_string_error(errors, "metadata.scenario_location_id", metadata.scenario_location_id);
  append_string_error(errors, "metadata.scenario_start_mode", metadata.scenario_start_mode);
  append_string_error(errors, "metadata.session_id", metadata.session_id);

  const DeterminismTolerance& tolerance = metadata.deterministic_tolerance;
  if (!is_non_negative_finite(tolerance.simulation_time_s) ||
      !is_non_negative_finite(tolerance.position_m) ||
      !is_non_negative_finite(tolerance.velocity_mps) ||
      !is_non_negative_finite(tolerance.quaternion_component) ||
      !is_non_negative_finite(tolerance.angular_velocity_radps) ||
      !is_non_negative_finite(tolerance.force_n) ||
      !is_non_negative_finite(tolerance.moment_nm)) {
    errors.push_back("deterministic tolerance fields must be non-negative finite values");
  }

  if (metadata.data_packages.empty()) {
    errors.push_back("at least one data package version must be recorded");
  }
  for (const DataPackageVersion& package : metadata.data_packages) {
    if (package.package_id.empty() || package.version.empty()) {
      errors.push_back("data package id and version must be recorded");
    }
    append_string_error(errors, "data_package.package_id", package.package_id);
    append_string_error(errors, "data_package.version", package.version);
  }

  if (!valid_rigid_body_parameters(recording.rigid_body_parameters)) {
    errors.push_back("rigid body mass and inertia parameters must be positive finite SI values");
  }
  if (!valid_state(recording.initial_state)) {
    errors.push_back("initial authoritative state must contain finite SI values");
  }
  try {
    validate_flight_dynamics_initial_condition(recording.initial_flight_dynamics);
  } catch (const std::exception& error) {
    errors.push_back(std::string("initial flight dynamics condition is invalid: ") + error.what());
  }
  try {
    validate_aircraft_controls(recording.initial_aircraft_controls);
  } catch (const std::exception& error) {
    errors.push_back(std::string("initial aircraft controls are invalid: ") + error.what());
  }

  for (std::size_t index = 0; index < recording.frames.size(); ++index) {
    const TelemetryFrame& frame = recording.frames[index];
    const std::string context = "frame " + std::to_string(index);
    if (frame.frame_index != index) {
      errors.push_back(context + " frame_index must be sequential");
    }
    if (!is_non_negative_finite(frame.caller_delta_s)) {
      errors.push_back(context + " caller_delta_s must be non-negative and finite");
    }
    if (!std::isfinite(frame.fixed_step_s) || frame.fixed_step_s <= 0.0) {
      errors.push_back(context + " fixed_step_s must be positive and finite");
    }
    if (!is_non_negative_finite(frame.consumed_time_s) ||
        !is_non_negative_finite(frame.remaining_accumulator_s)) {
      errors.push_back(context + " consumed and remaining accumulator time must be non-negative");
    }
    if (!valid_core_input(frame.core_input)) {
      errors.push_back(context + " core input forces and moments must be finite");
    }
    try {
      validate_aircraft_controls(frame.aircraft_controls);
    } catch (const std::exception& error) {
      errors.push_back(context + " aircraft controls are invalid: " + error.what());
    }
    if (!is_unit_interval(frame.engine.throttle_norm) ||
        !is_unit_interval(frame.engine.mixture_norm) ||
        !is_unit_interval(frame.engine.propeller_norm)) {
      errors.push_back(context + " engine normalized controls must be in [0, 1]");
    }
    if (!valid_state(frame.state)) {
      errors.push_back(context + " authoritative state must contain finite SI values");
    }
    if (frame.state_hash != hash_state(frame.state)) {
      errors.push_back(context + " state_hash must match authoritative state");
    }
  }

  return errors;
}

ReplayCompatibilityResult check_replay_compatibility(
    const TelemetryMetadata& metadata,
    const ReplayEnvironment& environment) {
  ReplayCompatibilityResult result{};

  if (metadata.schema_version != kTelemetrySchemaVersion ||
      metadata.schema_version != environment.telemetry_schema_version) {
    result.errors.push_back("telemetry schema version is incompatible: recording=" +
                            metadata.schema_version + " runtime=" +
                            environment.telemetry_schema_version);
  }
  if (metadata.core_sim_version != environment.core_sim_version) {
    result.errors.push_back("CoreSim version mismatch: recording=" +
                            metadata.core_sim_version + " runtime=" +
                            environment.core_sim_version);
  }
  if (!same_aircraft(metadata.aircraft, environment.aircraft)) {
    result.errors.push_back("aircraft configuration mismatch: recording=" +
                            metadata.aircraft.backend + "/" +
                            metadata.aircraft.model_name + "/" +
                            metadata.aircraft.model_version + " runtime=" +
                            environment.aircraft.backend + "/" +
                            environment.aircraft.model_name + "/" +
                            environment.aircraft.model_version);
  }

  for (const DataPackageVersion& recorded_package : metadata.data_packages) {
    const DataPackageVersion* runtime_package =
        find_package(recorded_package.package_id, environment.data_packages);
    if (!runtime_package) {
      result.errors.push_back("data package missing at replay: " +
                              recorded_package.package_id + "@" + recorded_package.version);
      continue;
    }
    if (runtime_package->version != recorded_package.version) {
      result.errors.push_back("data package version mismatch: " +
                              recorded_package.package_id + " recording=" +
                              recorded_package.version + " runtime=" +
                              runtime_package->version);
    }
  }

  result.compatible = result.errors.empty();
  if (!result.compatible) {
    result.warnings = result.errors;
  }
  return result;
}

ReplayResult replay_recording(const TelemetryRecording& recording,
                              CoreSimulator& simulator,
                              const ReplayEnvironment& environment,
                              ReplayCompatibilityPolicy compatibility_policy) {
  ReplayResult result{};
  result.deterministic = false;

  const std::vector<std::string> validation_errors = validate_telemetry_recording(recording);
  if (!validation_errors.empty()) {
    result.errors = validation_errors;
    result.refused = true;
    return result;
  }

  ReplayCompatibilityResult compatibility =
      check_replay_compatibility(recording.metadata, environment);
  if (!same_rigid_body_parameters(recording.rigid_body_parameters,
                                  environment.rigid_body_parameters)) {
    compatibility.errors.push_back("rigid body parameter mismatch between recording and replay runtime");
    compatibility.compatible = false;
    compatibility.warnings = compatibility.errors;
  }
  result.warnings = compatibility.warnings;
  if (!compatibility.compatible &&
      compatibility_policy == ReplayCompatibilityPolicy::RefuseOnMismatch) {
    result.errors = compatibility.errors;
    result.refused = true;
    return result;
  }

  simulator.reset(recording.initial_state,
                  recording.initial_flight_dynamics,
                  recording.initial_aircraft_controls);

  for (const TelemetryFrame& expected : recording.frames) {
    const AdvanceReport report = simulator.advance(expected.caller_delta_s, expected.core_input);
    const AuthoritativeState& actual_state = simulator.state();
    const std::uint64_t actual_hash = hash_state(actual_state);
    double max_state_error = 0.0;
    const bool within_tolerance = state_within_tolerance(
        expected.state,
        actual_state,
        recording.metadata.deterministic_tolerance,
        max_state_error);
    const bool scheduler_matched =
        report.steps_executed == expected.steps_executed &&
        report.total_steps == expected.total_steps;

    if (!scheduler_matched || (actual_hash != expected.state_hash && !within_tolerance)) {
      ReplayMismatch mismatch{};
      mismatch.frame_index = expected.frame_index;
      mismatch.expected_state_hash = expected.state_hash;
      mismatch.actual_state_hash = actual_hash;
      mismatch.max_state_error = max_state_error;
      mismatch.reason = !scheduler_matched
                            ? "fixed-step scheduler did not reproduce recorded step counts"
                            : "state hash mismatch exceeded deterministic tolerance";
      result.mismatches.push_back(std::move(mismatch));
    }

    result.final_state_hash = actual_hash;
    ++result.replayed_frames;
  }

  result.played = true;
  result.deterministic = result.mismatches.empty();
  return result;
}

TelemetryWriteResult save_telemetry_file_atomic(const std::filesystem::path& path,
                                                const TelemetryRecording& recording) {
  TelemetryWriteResult result{};
  result.errors = validate_telemetry_recording(recording);
  if (!result.errors.empty()) {
    return result;
  }

  std::ostringstream content;
  write_recording(content, recording);
  return write_text_file_atomic(path, content.str(), "telemetry");
}

TelemetryLoadResult load_telemetry_file(const std::filesystem::path& path) {
  TelemetryLoadResult result{};

  try {
    std::ifstream input(path);
    if (!input) {
      result.errors.push_back("unable to open telemetry file");
      return result;
    }

    std::string header;
    if (!std::getline(input, header) || header != kTelemetrySchemaVersion) {
      result.errors.push_back("telemetry schema header is missing or unsupported");
      return result;
    }

    TelemetryRecording recording{};
    recording.metadata = make_default_telemetry_metadata();
    recording.metadata.schema_version = std::string(kTelemetrySchemaVersion);
    recording.metadata.data_packages.clear();
    bool saw_metadata = false;
    bool saw_rigid_body_parameters = false;
    bool saw_initial_state = false;
    bool saw_initial_flight_dynamics = false;
    bool saw_initial_aircraft_controls = false;

    std::string line;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
      ++line_number;
      if (line.empty()) {
        continue;
      }

      const std::vector<std::string_view> fields = split_fields(line);
      if (fields.empty()) {
        continue;
      }

      if (fields[0] == "metadata") {
        if (fields.size() != 21) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " metadata record is malformed");
          continue;
        }

        std::size_t index = 1;
        recording.metadata.core_sim_version = to_owned(fields[index++]);
        recording.metadata.aircraft.backend = to_owned(fields[index++]);
        recording.metadata.aircraft.model_name = to_owned(fields[index++]);
        recording.metadata.aircraft.model_version = to_owned(fields[index++]);
        recording.metadata.aircraft.data_root = to_owned(fields[index++]);
        recording.metadata.aircraft.source_license = to_owned(fields[index++]);
        recording.metadata.simulation_configuration_id = to_owned(fields[index++]);
        recording.metadata.input_profile_id = to_owned(fields[index++]);
        recording.metadata.scenario_location_id = to_owned(fields[index++]);
        recording.metadata.scenario_start_mode = to_owned(fields[index++]);
        recording.metadata.session_id = to_owned(fields[index++]);
        const bool parsed =
            read_i64(fields, index, recording.metadata.started_unix_ms) &&
            read_double(fields, index, recording.metadata.fixed_step_s) &&
            read_double(fields, index,
                        recording.metadata.deterministic_tolerance.simulation_time_s) &&
            read_double(fields, index,
                        recording.metadata.deterministic_tolerance.position_m) &&
            read_double(fields, index,
                        recording.metadata.deterministic_tolerance.velocity_mps) &&
            read_double(fields, index,
                        recording.metadata.deterministic_tolerance.quaternion_component) &&
            read_double(fields, index,
                        recording.metadata.deterministic_tolerance.angular_velocity_radps) &&
            read_double(fields, index,
                        recording.metadata.deterministic_tolerance.force_n) &&
            read_double(fields, index,
                        recording.metadata.deterministic_tolerance.moment_nm);
        if (!parsed || index != fields.size()) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " metadata numeric fields are invalid");
          continue;
        }
        saw_metadata = true;
      } else if (fields[0] == "data_package") {
        if (fields.size() != 3) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " data_package record is malformed");
          continue;
        }
        recording.metadata.data_packages.push_back({to_owned(fields[1]), to_owned(fields[2])});
      } else if (fields[0] == "rigid_body_parameters") {
        if (fields.size() != 5) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " rigid_body_parameters record is malformed");
          continue;
        }
        std::size_t index = 1;
        if (!read_rigid_body_parameters(fields, index, recording.rigid_body_parameters) ||
            index != fields.size()) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " rigid_body_parameters record is invalid");
          continue;
        }
        saw_rigid_body_parameters = true;
      } else if (fields[0] == "initial_state") {
        const bool legacy_state_record = fields.size() == kLegacyInitialStateFieldCount;
        if (!is_initial_state_field_count_supported(fields.size())) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " initial_state record is malformed");
          continue;
        }
        std::size_t index = 1;
        std::uint64_t recorded_hash = 0;
        const bool parsed_state =
            legacy_state_record
                ? read_legacy_state(fields, index, recording.initial_state)
                : read_state(fields, index, recording.initial_state);
        if (!parsed_state ||
            !read_u64(fields, index, recorded_hash) ||
            index != fields.size()) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " initial_state record is invalid");
          continue;
        }
        const std::uint64_t expected_hash =
            legacy_state_record ? hash_legacy_state(recording.initial_state)
                                : hash_state(recording.initial_state);
        if (recorded_hash != expected_hash) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " initial_state hash does not match state");
        }
        saw_initial_state = true;
      } else if (fields[0] == "initial_flight_dynamics") {
        if (fields.size() != 14) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " initial_flight_dynamics record is malformed");
          continue;
        }
        std::size_t index = 1;
        if (!read_flight_dynamics_initial_condition(
                fields,
                index,
                recording.initial_flight_dynamics) ||
            index != fields.size()) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " initial_flight_dynamics record is invalid");
          continue;
        }
        saw_initial_flight_dynamics = true;
      } else if (fields[0] == "initial_aircraft_controls") {
        if (fields.size() != 13) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " initial_aircraft_controls record is malformed");
          continue;
        }
        std::size_t index = 1;
        if (!read_controls(fields, index, recording.initial_aircraft_controls) ||
            index != fields.size()) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " initial_aircraft_controls record is invalid");
          continue;
        }
        saw_initial_aircraft_controls = true;
      } else if (fields[0] == "frame") {
        const bool legacy_frame_record = fields.size() == kLegacyFrameFieldCount;
        if (!is_frame_field_count_supported(fields.size())) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " frame record is malformed");
          continue;
        }
        TelemetryFrame frame{};
        std::size_t index = 1;
        if (!read_u64(fields, index, frame.frame_index) ||
            !read_i64(fields, index, frame.host_time_unix_ms) ||
            !read_double(fields, index, frame.caller_delta_s) ||
            !read_u32(fields, index, frame.steps_executed) ||
            !read_double(fields, index, frame.fixed_step_s) ||
            !read_double(fields, index, frame.consumed_time_s) ||
            !read_double(fields, index, frame.remaining_accumulator_s) ||
            !read_u64(fields, index, frame.total_steps) ||
            !read_core_input(fields, index, frame.core_input) ||
            !read_controls(fields, index, frame.aircraft_controls) ||
            !read_engine(fields, index, frame.engine) ||
            !(legacy_frame_record
                  ? read_legacy_state(fields, index, frame.state)
                  : read_state(fields, index, frame.state)) ||
            !read_u64(fields, index, frame.state_hash) ||
            index != fields.size()) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " frame record is invalid");
          continue;
        }
        const std::uint64_t expected_hash =
            legacy_frame_record ? hash_legacy_state(frame.state) : hash_state(frame.state);
        if (frame.state_hash != expected_hash) {
          result.errors.push_back("line " + std::to_string(line_number) +
                                  " frame state hash does not match state");
          continue;
        }
        if (legacy_frame_record) {
          frame.state_hash = hash_state(frame.state);
        }
        recording.frames.push_back(frame);
      } else {
        result.errors.push_back("line " + std::to_string(line_number) +
                                " telemetry record type is unsupported");
      }
    }

    if (!saw_metadata) {
      result.errors.push_back("telemetry metadata record is missing");
    }
    if (!saw_rigid_body_parameters) {
      result.errors.push_back("telemetry rigid_body_parameters record is missing");
    }
    if (!saw_initial_state) {
      result.errors.push_back("telemetry initial_state record is missing");
    }
    if (!saw_initial_flight_dynamics) {
      result.errors.push_back("telemetry initial_flight_dynamics record is missing");
    }
    if (!saw_initial_aircraft_controls) {
      result.errors.push_back("telemetry initial_aircraft_controls record is missing");
    }

    const std::vector<std::string> validation_errors = validate_telemetry_recording(recording);
    result.errors.insert(result.errors.end(), validation_errors.begin(), validation_errors.end());
    if (!result.errors.empty()) {
      return result;
    }

    result.loaded = true;
    result.recording = std::move(recording);
    return result;
  } catch (const std::exception& error) {
    result.errors.push_back(error.what());
    return result;
  }
}

TelemetryExportResult export_telemetry_csv(const std::filesystem::path& path,
                                           const TelemetryRecording& recording) {
  TelemetryExportResult result{};
  result.errors = validate_telemetry_recording(recording);
  if (!result.errors.empty()) {
    return result;
  }

  std::ostringstream output;
  append_csv_metadata_string(output, "schema_version", kTelemetryCsvExportSchemaVersion);
  append_csv_metadata_string(output, "telemetry_schema_version", recording.metadata.schema_version);
  append_csv_metadata_string(output, "core_sim_version", recording.metadata.core_sim_version);
  append_csv_metadata_string(output, "aircraft_backend", recording.metadata.aircraft.backend);
  append_csv_metadata_string(output, "aircraft_model", recording.metadata.aircraft.model_name);
  append_csv_metadata_string(output, "aircraft_version", recording.metadata.aircraft.model_version);
  append_csv_metadata_string(output, "aircraft_data_root", recording.metadata.aircraft.data_root);
  append_csv_metadata_string(output, "aircraft_source_license", recording.metadata.aircraft.source_license);
  output << "# rigid_body_mass_kg," << double_to_string(recording.rigid_body_parameters.mass_kg) << '\n';
  output << "# rigid_body_inertia_kg_m2,"
         << double_to_string(recording.rigid_body_parameters.inertia_diagonal_kg_m2.x) << ','
         << double_to_string(recording.rigid_body_parameters.inertia_diagonal_kg_m2.y) << ','
         << double_to_string(recording.rigid_body_parameters.inertia_diagonal_kg_m2.z) << '\n';
  append_csv_metadata_string(
      output,
      "simulation_configuration_id",
      recording.metadata.simulation_configuration_id);
  append_csv_metadata_string(output, "input_profile_id", recording.metadata.input_profile_id);
  append_csv_metadata_string(output, "scenario_location_id", recording.metadata.scenario_location_id);
  append_csv_metadata_string(output, "scenario_start_mode", recording.metadata.scenario_start_mode);
  append_csv_metadata_string(output, "session_id", recording.metadata.session_id);
  output << "# started_unix_ms," << recording.metadata.started_unix_ms << '\n';
  output << "# fixed_step_s," << double_to_string(recording.metadata.fixed_step_s) << '\n';
  for (const DataPackageVersion& package : recording.metadata.data_packages) {
    output << "# data_package,";
    append_csv_string(output, package.package_id);
    output << ',';
    append_csv_string(output, package.version);
    output << '\n';
  }

  output << "frame_index,host_time_unix_ms,simulation_time_s,step_index,"
         << "caller_delta_s,steps_executed,fixed_step_s,remaining_accumulator_s,"
         << "state_hash,ecef_x_m,ecef_y_m,ecef_z_m,ecef_vx_mps,ecef_vy_mps,ecef_vz_mps,"
         << "quat_w,quat_x,quat_y,quat_z,angular_velocity_x_radps,angular_velocity_y_radps,"
         << "angular_velocity_z_radps,force_x_n,force_y_n,force_z_n,moment_x_nm,"
         << "moment_y_nm,moment_z_nm,total_mass_kg,fuel_mass_kg,payload_mass_kg,"
         << "cg_x_m,cg_y_m,cg_z_m,inertia_ixx_kg_m2,inertia_iyy_kg_m2,"
         << "inertia_izz_kg_m2,inertia_ixy_kg_m2,inertia_ixz_kg_m2,"
         << "inertia_iyz_kg_m2,cg_within_envelope,input_force_x_n,input_force_y_n,input_force_z_n,"
         << "input_moment_x_nm,input_moment_y_nm,input_moment_z_nm,aileron_norm,"
         << "elevator_norm,rudder_norm,throttle_norm,flaps_norm,brake_left_norm,"
         << "brake_right_norm,mixture_norm,propeller_norm,elevator_trim_norm,"
         << "aileron_trim_norm,rudder_trim_norm,engine_running,engine_throttle_norm,"
         << "engine_mixture_norm,engine_propeller_norm\n";

  output << std::setprecision(std::numeric_limits<double>::max_digits10);
  for (const TelemetryFrame& frame : recording.frames) {
    const AuthoritativeState& state = frame.state;
    output << frame.frame_index << ','
           << frame.host_time_unix_ms << ','
           << state.simulation_time_s << ','
           << state.step_index << ','
           << frame.caller_delta_s << ','
           << frame.steps_executed << ','
           << frame.fixed_step_s << ','
           << frame.remaining_accumulator_s << ','
           << u64_to_hex(frame.state_hash) << ','
           << state.ecef_position_m.x << ','
           << state.ecef_position_m.y << ','
           << state.ecef_position_m.z << ','
           << state.ecef_velocity_mps.x << ','
           << state.ecef_velocity_mps.y << ','
           << state.ecef_velocity_mps.z << ','
           << state.body_to_ecef.w << ','
           << state.body_to_ecef.x << ','
           << state.body_to_ecef.y << ','
           << state.body_to_ecef.z << ','
           << state.angular_velocity_body_radps.x << ','
           << state.angular_velocity_body_radps.y << ','
           << state.angular_velocity_body_radps.z << ','
           << state.accumulated_force_body_n.x << ','
           << state.accumulated_force_body_n.y << ','
           << state.accumulated_force_body_n.z << ','
           << state.accumulated_moment_body_nm.x << ','
           << state.accumulated_moment_body_nm.y << ','
           << state.accumulated_moment_body_nm.z << ','
           << state.aircraft_mass_balance.total_mass_kg << ','
           << state.aircraft_mass_balance.fuel_mass_kg << ','
           << state.aircraft_mass_balance.payload_mass_kg << ','
           << state.aircraft_mass_balance.center_of_gravity_body_m.x << ','
           << state.aircraft_mass_balance.center_of_gravity_body_m.y << ','
           << state.aircraft_mass_balance.center_of_gravity_body_m.z << ','
           << state.aircraft_mass_balance.inertia_tensor_kg_m2.ixx << ','
           << state.aircraft_mass_balance.inertia_tensor_kg_m2.iyy << ','
           << state.aircraft_mass_balance.inertia_tensor_kg_m2.izz << ','
           << state.aircraft_mass_balance.inertia_tensor_kg_m2.ixy << ','
           << state.aircraft_mass_balance.inertia_tensor_kg_m2.ixz << ','
           << state.aircraft_mass_balance.inertia_tensor_kg_m2.iyz << ','
           << (state.aircraft_mass_balance.cg_within_envelope ? 1 : 0) << ','
           << frame.core_input.force_body_n.x << ','
           << frame.core_input.force_body_n.y << ','
           << frame.core_input.force_body_n.z << ','
           << frame.core_input.moment_body_nm.x << ','
           << frame.core_input.moment_body_nm.y << ','
           << frame.core_input.moment_body_nm.z << ','
           << frame.aircraft_controls.aileron_norm << ','
           << frame.aircraft_controls.elevator_norm << ','
           << frame.aircraft_controls.rudder_norm << ','
           << frame.aircraft_controls.throttle_norm << ','
           << frame.aircraft_controls.flaps_norm << ','
           << frame.aircraft_controls.brake_left_norm << ','
           << frame.aircraft_controls.brake_right_norm << ','
           << frame.aircraft_controls.mixture_norm << ','
           << frame.aircraft_controls.propeller_norm << ','
           << frame.aircraft_controls.elevator_trim_norm << ','
           << frame.aircraft_controls.aileron_trim_norm << ','
           << frame.aircraft_controls.rudder_trim_norm << ','
           << (frame.engine.engine_running ? 1 : 0) << ','
           << frame.engine.throttle_norm << ','
           << frame.engine.mixture_norm << ','
           << frame.engine.propeller_norm << '\n';
  }

  return to_export_result(write_text_file_atomic(path, output.str(), "telemetry CSV export"));
}

TelemetryExportResult export_telemetry_json(const std::filesystem::path& path,
                                            const TelemetryRecording& recording) {
  TelemetryExportResult result{};
  result.errors = validate_telemetry_recording(recording);
  if (!result.errors.empty()) {
    return result;
  }

  std::ostringstream output;
  output << std::setprecision(std::numeric_limits<double>::max_digits10);
  output << "{\"schema_version\":";
  append_json_string(output, kTelemetryJsonExportSchemaVersion);
  output << ",\"telemetry_schema_version\":";
  append_json_string(output, recording.metadata.schema_version);
  output << ",\"metadata\":{\"core_sim_version\":";
  append_json_string(output, recording.metadata.core_sim_version);
  output << ",\"aircraft\":{\"backend\":";
  append_json_string(output, recording.metadata.aircraft.backend);
  output << ",\"model_name\":";
  append_json_string(output, recording.metadata.aircraft.model_name);
  output << ",\"model_version\":";
  append_json_string(output, recording.metadata.aircraft.model_version);
  output << ",\"data_root\":";
  append_json_string(output, recording.metadata.aircraft.data_root);
  output << ",\"source_license\":";
  append_json_string(output, recording.metadata.aircraft.source_license);
  output << "},\"rigid_body_parameters\":{\"mass_kg\":"
         << double_to_string(recording.rigid_body_parameters.mass_kg)
         << ",\"inertia_diagonal_kg_m2\":";
  append_json_vector(output, recording.rigid_body_parameters.inertia_diagonal_kg_m2);
  output << "},\"simulation_configuration_id\":";
  append_json_string(output, recording.metadata.simulation_configuration_id);
  output << ",\"input_profile_id\":";
  append_json_string(output, recording.metadata.input_profile_id);
  output << ",\"scenario_location_id\":";
  append_json_string(output, recording.metadata.scenario_location_id);
  output << ",\"scenario_start_mode\":";
  append_json_string(output, recording.metadata.scenario_start_mode);
  output << ",\"session_id\":";
  append_json_string(output, recording.metadata.session_id);
  output << ",\"started_unix_ms\":" << recording.metadata.started_unix_ms
         << ",\"fixed_step_s\":" << double_to_string(recording.metadata.fixed_step_s)
         << ",\"deterministic_tolerance\":{\"simulation_time_s\":"
         << double_to_string(recording.metadata.deterministic_tolerance.simulation_time_s)
         << ",\"position_m\":"
         << double_to_string(recording.metadata.deterministic_tolerance.position_m)
         << ",\"velocity_mps\":"
         << double_to_string(recording.metadata.deterministic_tolerance.velocity_mps)
         << ",\"quaternion_component\":"
         << double_to_string(recording.metadata.deterministic_tolerance.quaternion_component)
         << ",\"angular_velocity_radps\":"
         << double_to_string(recording.metadata.deterministic_tolerance.angular_velocity_radps)
         << ",\"force_n\":"
         << double_to_string(recording.metadata.deterministic_tolerance.force_n)
         << ",\"moment_nm\":"
         << double_to_string(recording.metadata.deterministic_tolerance.moment_nm)
         << "},\"data_packages\":[";
  for (std::size_t index = 0; index < recording.metadata.data_packages.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    const DataPackageVersion& package = recording.metadata.data_packages[index];
    output << "{\"package_id\":";
    append_json_string(output, package.package_id);
    output << ",\"version\":";
    append_json_string(output, package.version);
    output << '}';
  }
  output << "]},\"initial_state\":";
  append_json_state(output, recording.initial_state);
  output << ",\"initial_aircraft_controls\":";
  append_json_aircraft_controls(output, recording.initial_aircraft_controls);
  output << ",\"flight_path\":[";
  for (std::size_t index = 0; index < recording.frames.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    const TelemetryFrame& frame = recording.frames[index];
    output << "{\"frame_index\":" << frame.frame_index
           << ",\"simulation_time_s\":" << double_to_string(frame.state.simulation_time_s)
           << ",\"step_index\":" << frame.state.step_index
           << ",\"ecef_position_m\":";
    append_json_vector(output, frame.state.ecef_position_m);
    output << ",\"state_hash\":";
    append_json_string(output, u64_to_hex(frame.state_hash));
    output << '}';
  }
  output << "],\"frames\":[";
  for (std::size_t index = 0; index < recording.frames.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    const TelemetryFrame& frame = recording.frames[index];
    output << "{\"frame_index\":" << frame.frame_index
           << ",\"host_time_unix_ms\":" << frame.host_time_unix_ms
           << ",\"caller_delta_s\":" << double_to_string(frame.caller_delta_s)
           << ",\"steps_executed\":" << frame.steps_executed
           << ",\"fixed_step_s\":" << double_to_string(frame.fixed_step_s)
           << ",\"remaining_accumulator_s\":"
           << double_to_string(frame.remaining_accumulator_s)
           << ",\"state_hash\":";
    append_json_string(output, u64_to_hex(frame.state_hash));
    output << ",\"inputs\":{\"core\":";
    append_json_core_input(output, frame.core_input);
    output << ",\"aircraft_controls\":";
    append_json_aircraft_controls(output, frame.aircraft_controls);
    output << "},\"engine\":";
    append_json_engine(output, frame.engine);
    output << ",\"state\":";
    append_json_state(output, frame.state);
    output << '}';
  }
  output << "]}";

  return to_export_result(write_text_file_atomic(path, output.str(), "telemetry JSON export"));
}

} // namespace flying::core_sim
