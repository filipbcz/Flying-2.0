#pragma once

#include "flying/core_sim/state.hpp"

#include <string>

namespace flying::presentation {

enum class CameraMode {
  Cockpit,
  External,
};

struct CameraAuthoritativeView {
  CameraMode mode{CameraMode::Cockpit};
  std::string source{"CoreSim.AuthoritativeState"};
  std::uint64_t authoritative_step_index{};
  core_sim::Vector3d authoritative_ecef_position_m{};
  core_sim::Quaterniond authoritative_body_to_ecef{};
  core_sim::Vector3d camera_offset_body_m{};
  double field_of_view_deg{};
};

[[nodiscard]] CameraAuthoritativeView make_camera_view(
    CameraMode mode,
    const core_sim::AuthoritativeState& state) noexcept;

[[nodiscard]] bool camera_views_share_authoritative_aircraft_state(
    const CameraAuthoritativeView& first,
    const CameraAuthoritativeView& second) noexcept;

} // namespace flying::presentation
