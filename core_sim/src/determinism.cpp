#include "flying/core_sim/determinism.hpp"

#include <bit>
#include <cmath>
#include <cstdint>

namespace flying::core_sim {
namespace {

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ull;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ull;
constexpr std::uint64_t kHashSchemaVersion = 1;

[[nodiscard]] std::uint64_t append_u64(std::uint64_t hash, std::uint64_t value) noexcept {
  for (int byte_index = 0; byte_index < 8; ++byte_index) {
    const auto byte = static_cast<std::uint8_t>((value >> (byte_index * 8)) & 0xffu);
    hash ^= byte;
    hash *= kFnvPrime;
  }
  return hash;
}

[[nodiscard]] std::uint64_t canonical_double_bits(double value) noexcept {
  if (value == 0.0) {
    value = 0.0;
  }
  if (std::isnan(value)) {
    return 0x7ff8'0000'0000'0000ull;
  }
  return std::bit_cast<std::uint64_t>(value);
}

[[nodiscard]] std::uint64_t append_double(std::uint64_t hash, double value) noexcept {
  return append_u64(hash, canonical_double_bits(value));
}

[[nodiscard]] std::uint64_t append_vector(std::uint64_t hash, Vector3d value) noexcept {
  hash = append_double(hash, value.x);
  hash = append_double(hash, value.y);
  return append_double(hash, value.z);
}

[[nodiscard]] std::uint64_t append_quaternion(std::uint64_t hash, Quaterniond value) noexcept {
  hash = append_double(hash, value.w);
  hash = append_double(hash, value.x);
  hash = append_double(hash, value.y);
  return append_double(hash, value.z);
}

} // namespace

std::uint64_t hash_state(const AuthoritativeState& state) noexcept {
  auto hash = append_u64(kFnvOffset, kHashSchemaVersion);
  hash = append_double(hash, state.simulation_time_s);
  hash = append_u64(hash, state.step_index);
  hash = append_vector(hash, state.ecef_position_m);
  hash = append_vector(hash, state.ecef_velocity_mps);
  hash = append_quaternion(hash, state.body_to_ecef);
  hash = append_vector(hash, state.angular_velocity_body_radps);
  hash = append_vector(hash, state.accumulated_force_body_n);
  return append_vector(hash, state.accumulated_moment_body_nm);
}

} // namespace flying::core_sim
