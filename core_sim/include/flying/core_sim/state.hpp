#pragma once

#include "flying/core_sim/math.hpp"

#include <cstdint>

namespace flying::core_sim {

struct AircraftInertiaTensor {
  double ixx{};
  double iyy{};
  double izz{};
  double ixy{};
  double ixz{};
  double iyz{};
};

struct AircraftMassBalanceState {
  double total_mass_kg{1'000.0};
  double fuel_mass_kg{};
  double payload_mass_kg{};
  Vector3d center_of_gravity_body_m{};
  AircraftInertiaTensor inertia_tensor_kg_m2{800.0, 1'100.0, 1'400.0, 0.0, 0.0, 0.0};
  bool cg_within_envelope{true};
};

struct RigidBodyParameters {
  double mass_kg{1'000.0};
  Vector3d inertia_diagonal_kg_m2{800.0, 1'100.0, 1'400.0};
};

struct AuthoritativeState {
  double simulation_time_s{};
  std::uint64_t step_index{};

  Vector3d ecef_position_m{};
  Vector3d ecef_velocity_mps{};
  Quaterniond body_to_ecef{};

  Vector3d angular_velocity_body_radps{};
  Vector3d accumulated_force_body_n{};
  Vector3d accumulated_moment_body_nm{};
  AircraftMassBalanceState aircraft_mass_balance{};
};

} // namespace flying::core_sim
