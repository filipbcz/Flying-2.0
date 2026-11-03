#pragma once

#include "flying/core_sim/api.hpp"

#include "flying/core_sim/state.hpp"

#include <cstdint>

namespace flying::core_sim {

[[nodiscard]] FLYING_CORE_SIM_API std::uint64_t hash_state(const AuthoritativeState& state) noexcept;

} // namespace flying::core_sim
