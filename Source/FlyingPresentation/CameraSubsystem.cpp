#include "CameraSubsystem.h"

namespace flying::presentation {

CameraAuthoritativeView make_camera_view(
    CameraMode mode,
    const core_sim::AuthoritativeState& state) noexcept {
  CameraAuthoritativeView view{};
  view.mode = mode;
  view.authoritative_step_index = state.step_index;
  view.authoritative_ecef_position_m = state.ecef_position_m;
  view.authoritative_body_to_ecef = state.body_to_ecef;

  if (mode == CameraMode::External) {
    view.camera_offset_body_m = {-7.0, -12.0, 3.0};
    view.field_of_view_deg = 60.0;
  } else {
    view.camera_offset_body_m = {0.45, 0.0, 1.12};
    view.field_of_view_deg = 72.0;
  }
  return view;
}

bool camera_views_share_authoritative_aircraft_state(
    const CameraAuthoritativeView& first,
    const CameraAuthoritativeView& second) noexcept {
  return first.source == second.source &&
         first.authoritative_step_index == second.authoritative_step_index &&
         first.authoritative_ecef_position_m.x == second.authoritative_ecef_position_m.x &&
         first.authoritative_ecef_position_m.y == second.authoritative_ecef_position_m.y &&
         first.authoritative_ecef_position_m.z == second.authoritative_ecef_position_m.z &&
         first.authoritative_body_to_ecef.w == second.authoritative_body_to_ecef.w &&
         first.authoritative_body_to_ecef.x == second.authoritative_body_to_ecef.x &&
         first.authoritative_body_to_ecef.y == second.authoritative_body_to_ecef.y &&
         first.authoritative_body_to_ecef.z == second.authoritative_body_to_ecef.z;
}

} // namespace flying::presentation
