#include "flying/core_sim/jsbsim_adapter.hpp"

#include <FGFDMExec.h>
#include <initialization/FGInitialCondition.h>
#include <math/FGMatrix33.h>
#include <models/FGPropagate.h>
#include <simgear/misc/sg_path.hxx>

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace flying::core_sim {
namespace {

constexpr double kFeetToMeters = 0.3048;
constexpr double kMetersToFeet = 1.0 / kFeetToMeters;
constexpr double kPoundForceToNewtons = 4.4482216152605;
constexpr double kFootPoundToNewtonMeters = 1.3558179483314004;
constexpr double kKnotsToMetersPerSecond = 0.5144444444444445;
constexpr double kRadiansToDegrees = 180.0 / 3.141592653589793238462643383279502884;
constexpr double kStepToleranceSeconds = 1.0e-15;

[[nodiscard]] std::filesystem::path default_jsbsim_data_root() {
#ifdef FLYING_CORE_SIM_JSBSIM_DATA_DIR
  return std::filesystem::path{FLYING_CORE_SIM_JSBSIM_DATA_DIR};
#else
  return std::filesystem::path{"core_sim/jsbsim"};
#endif
}

[[nodiscard]] std::string generic_string(const std::filesystem::path& path) {
  return path.generic_string();
}

[[nodiscard]] SGPath sg_path(const std::filesystem::path& path) {
  return SGPath{generic_string(path)};
}

[[nodiscard]] double require_property(JSBSim::FGFDMExec& executive, const char* property_name) {
  const double value = executive.GetPropertyValue(property_name);
  if (!std::isfinite(value)) {
    throw std::runtime_error(std::string("JSBSim property is not finite: ") + property_name);
  }
  return value;
}

void set_property(JSBSim::FGFDMExec& executive, const char* property_name, double value) {
  if (!std::isfinite(value)) {
    throw std::invalid_argument(std::string("JSBSim property value is not finite: ") + property_name);
  }
  executive.SetPropertyValue(property_name, value);
}

[[nodiscard]] Quaterniond quaternion_from_body_to_ecef(const JSBSim::FGMatrix33& matrix) noexcept {
  const double m00 = matrix(1, 1);
  const double m01 = matrix(1, 2);
  const double m02 = matrix(1, 3);
  const double m10 = matrix(2, 1);
  const double m11 = matrix(2, 2);
  const double m12 = matrix(2, 3);
  const double m20 = matrix(3, 1);
  const double m21 = matrix(3, 2);
  const double m22 = matrix(3, 3);

  Quaterniond quaternion{};
  const double trace = m00 + m11 + m22;
  if (trace > 0.0) {
    const double scale = std::sqrt(trace + 1.0) * 2.0;
    quaternion.w = 0.25 * scale;
    quaternion.x = (m21 - m12) / scale;
    quaternion.y = (m02 - m20) / scale;
    quaternion.z = (m10 - m01) / scale;
  } else if (m00 > m11 && m00 > m22) {
    const double scale = std::sqrt(1.0 + m00 - m11 - m22) * 2.0;
    quaternion.w = (m21 - m12) / scale;
    quaternion.x = 0.25 * scale;
    quaternion.y = (m01 + m10) / scale;
    quaternion.z = (m02 + m20) / scale;
  } else if (m11 > m22) {
    const double scale = std::sqrt(1.0 + m11 - m00 - m22) * 2.0;
    quaternion.w = (m02 - m20) / scale;
    quaternion.x = (m01 + m10) / scale;
    quaternion.y = 0.25 * scale;
    quaternion.z = (m12 + m21) / scale;
  } else {
    const double scale = std::sqrt(1.0 + m22 - m00 - m11) * 2.0;
    quaternion.w = (m10 - m01) / scale;
    quaternion.x = (m02 + m20) / scale;
    quaternion.y = (m12 + m21) / scale;
    quaternion.z = 0.25 * scale;
  }

  return quaternion.normalized();
}

[[nodiscard]] FlightDynamicsState extract_state(JSBSim::FGFDMExec& executive, std::uint64_t step_index) {
  const auto propagate = executive.GetPropagate();
  if (!propagate) {
    throw std::runtime_error("JSBSim propagate model is not available");
  }

  const auto ecef_velocity_fps = propagate->GetECEFVelocity();
  const auto euler = propagate->GetEuler();

  FlightDynamicsState state{};
  state.simulation_time_s = executive.GetSimTime();
  state.step_index = step_index;
  state.latitude_deg = propagate->GetGeodLatitudeDeg();
  state.longitude_deg = propagate->GetLongitudeDeg();
  state.altitude_m = propagate->GetGeodeticAltitude() * kFeetToMeters;
  state.terrain_elevation_m = propagate->GetTerrainElevation() * kFeetToMeters;
  state.ecef_position_m = {
    propagate->GetLocation(1) * kFeetToMeters,
    propagate->GetLocation(2) * kFeetToMeters,
    propagate->GetLocation(3) * kFeetToMeters,
  };
  state.ecef_velocity_mps = {
    ecef_velocity_fps(1) * kFeetToMeters,
    ecef_velocity_fps(2) * kFeetToMeters,
    ecef_velocity_fps(3) * kFeetToMeters,
  };
  state.ned_velocity_mps = {
    propagate->GetVel(1) * kFeetToMeters,
    propagate->GetVel(2) * kFeetToMeters,
    propagate->GetVel(3) * kFeetToMeters,
  };
  state.body_velocity_mps = {
    propagate->GetUVW(1) * kFeetToMeters,
    propagate->GetUVW(2) * kFeetToMeters,
    propagate->GetUVW(3) * kFeetToMeters,
  };
  state.body_to_ecef = quaternion_from_body_to_ecef(propagate->GetTb2ec());
  state.euler_rad = {euler(1), euler(2), euler(3)};
  state.angular_velocity_body_radps = {
    propagate->GetPQR(1),
    propagate->GetPQR(2),
    propagate->GetPQR(3),
  };
  state.total_force_body_n = {
    require_property(executive, "forces/fbx-total-lbs") * kPoundForceToNewtons,
    require_property(executive, "forces/fby-total-lbs") * kPoundForceToNewtons,
    require_property(executive, "forces/fbz-total-lbs") * kPoundForceToNewtons,
  };
  state.total_moment_body_nm = {
    require_property(executive, "moments/l-total-lbsft") * kFootPoundToNewtonMeters,
    require_property(executive, "moments/m-total-lbsft") * kFootPoundToNewtonMeters,
    require_property(executive, "moments/n-total-lbsft") * kFootPoundToNewtonMeters,
  };
  state.angle_of_attack_rad = require_property(executive, "aero/alpha-rad");
  state.sideslip_rad = require_property(executive, "aero/beta-rad");
  state.mach = require_property(executive, "velocities/mach");
  state.calibrated_airspeed_mps =
      require_property(executive, "velocities/vc-kts") * kKnotsToMetersPerSecond;

  return state;
}

void apply_initial_condition(JSBSim::FGFDMExec& executive,
                             const FlightDynamicsInitialCondition& initial_condition) {
  auto initial = executive.GetIC();
  if (!initial) {
    throw std::runtime_error("JSBSim initial condition model is not available");
  }

  initial->InitializeIC();
  initial->SetGeodLatitudeDegIC(initial_condition.latitude_deg);
  initial->SetLongitudeDegIC(initial_condition.longitude_deg);
  initial->SetAltitudeASLFtIC(initial_condition.altitude_m * kMetersToFeet);
  initial->SetTerrainElevationFtIC(initial_condition.terrain_elevation_m * kMetersToFeet);
  initial->SetUBodyFpsIC(initial_condition.body_velocity_mps.x * kMetersToFeet);
  initial->SetVBodyFpsIC(initial_condition.body_velocity_mps.y * kMetersToFeet);
  initial->SetWBodyFpsIC(initial_condition.body_velocity_mps.z * kMetersToFeet);
  initial->SetPRadpsIC(initial_condition.angular_velocity_body_radps.x);
  initial->SetQRadpsIC(initial_condition.angular_velocity_body_radps.y);
  initial->SetRRadpsIC(initial_condition.angular_velocity_body_radps.z);
  initial->SetPhiDegIC(initial_condition.roll_rad * kRadiansToDegrees);
  initial->SetThetaDegIC(initial_condition.pitch_rad * kRadiansToDegrees);
  initial->SetPsiDegIC(initial_condition.heading_rad * kRadiansToDegrees);

  executive.Setsim_time(0.0);
  if (!executive.RunIC()) {
    throw std::runtime_error("JSBSim failed to initialize aircraft state");
  }
}

void apply_controls(JSBSim::FGFDMExec& executive, const AircraftControlInputSample& controls) {
  set_property(executive, "fcs/aileron-cmd-norm", controls.aileron_norm);
  set_property(executive, "fcs/elevator-cmd-norm", controls.elevator_norm);
  set_property(executive, "fcs/rudder-cmd-norm", controls.rudder_norm);
  set_property(executive, "fcs/throttle-cmd-norm", controls.throttle_norm);
  set_property(executive, "fcs/throttle-cmd-norm[0]", controls.throttle_norm);
  set_property(executive, "fcs/flap-cmd-norm", controls.flaps_norm);
  set_property(executive, "fcs/left-brake-cmd-norm", controls.brake_left_norm);
  set_property(executive, "fcs/right-brake-cmd-norm", controls.brake_right_norm);
  set_property(executive, "fcs/mixture-cmd-norm", controls.mixture_norm);
  set_property(executive, "fcs/mixture-cmd-norm[0]", controls.mixture_norm);
}

[[nodiscard]] FlightDynamicsStepRecord make_record(
    const AircraftConfigurationIdentity& aircraft,
    const AircraftControlInputSample& controls,
    double fixed_step_s,
    const FlightDynamicsState& state) noexcept {
  FlightDynamicsStepRecord record{};
  record.step_index = state.step_index;
  record.fixed_step_s = fixed_step_s;
  record.aircraft = aircraft;
  record.controls = controls;
  record.state = state;
  record.state_hash = hash_flight_dynamics_state(record.state);
  record.record_hash = hash_flight_dynamics_step_record(record);
  return record;
}

} // namespace

JsbsimAircraftConfig placeholder_jsbsim_aircraft_config() {
  JsbsimAircraftConfig config{};
  config.root_dir = default_jsbsim_data_root();
  return config;
}

class JsbsimFlightDynamicsBackend::Impl {
public:
  explicit Impl(JsbsimAircraftConfig config)
      : config_(std::move(config)),
        identity_{
          "JSBSim",
          config_.model_name,
          config_.model_version,
          generic_string(config_.root_dir),
          config_.source_license,
        } {
    if (config_.root_dir.empty()) {
      throw std::invalid_argument("JSBSim root_dir must not be empty");
    }
    if (config_.model_name.empty()) {
      throw std::invalid_argument("JSBSim model_name must not be empty");
    }

    executive_.SetDebugLevel(0);
    executive_.SetRootDir(sg_path(config_.root_dir));
    executive_.SetAircraftPath(sg_path(config_.aircraft_path));
    executive_.SetEnginePath(sg_path(config_.engine_path));
    executive_.SetSystemsPath(sg_path(config_.systems_path));
    executive_.SetOutputPath(sg_path(std::filesystem::path{"."}));
    executive_.DisableOutput();
    executive_.Setdt(kFixedStepSeconds);
    set_property(executive_, "simulation/randomseed", 0.0);

    if (!executive_.LoadModel(config_.model_name, true)) {
      throw std::runtime_error("JSBSim failed to load aircraft model: " + config_.model_name);
    }

    reset({});
  }

  [[nodiscard]] const AircraftConfigurationIdentity& aircraft() const noexcept {
    return identity_;
  }

  [[nodiscard]] const FlightDynamicsState& state() const noexcept {
    return state_;
  }

  [[nodiscard]] const FlightDynamicsStepRecord& last_step_record() const noexcept {
    return last_step_record_;
  }

  void reset(const FlightDynamicsInitialCondition& initial_condition) {
    validate_flight_dynamics_initial_condition(initial_condition);
    executive_.Setdt(kFixedStepSeconds);
    apply_initial_condition(executive_, initial_condition);
    step_index_ = 0;
    state_ = extract_state(executive_, step_index_);
    last_step_record_ = make_record(identity_, {}, kFixedStepSeconds, state_);
  }

  FlightDynamicsStepRecord step_fixed(double fixed_step_s, const AircraftControlInputSample& controls) {
    validate_aircraft_controls(controls);
    if (!std::isfinite(fixed_step_s) ||
        std::abs(fixed_step_s - kFixedStepSeconds) > kStepToleranceSeconds) {
      throw std::invalid_argument("JSBSim adapter accepts only CoreSim 240 Hz fixed steps");
    }

    executive_.Setdt(fixed_step_s);
    apply_controls(executive_, controls);
    if (!executive_.Run()) {
      throw std::runtime_error("JSBSim reported a failed simulation step");
    }

    ++step_index_;
    state_ = extract_state(executive_, step_index_);
    last_step_record_ = make_record(identity_, controls, fixed_step_s, state_);
    return last_step_record_;
  }

private:
  JsbsimAircraftConfig config_;
  AircraftConfigurationIdentity identity_;
  JSBSim::FGFDMExec executive_;
  FlightDynamicsState state_{};
  FlightDynamicsStepRecord last_step_record_{};
  std::uint64_t step_index_{};
};

JsbsimFlightDynamicsBackend::JsbsimFlightDynamicsBackend(JsbsimAircraftConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

JsbsimFlightDynamicsBackend::~JsbsimFlightDynamicsBackend() = default;

JsbsimFlightDynamicsBackend::JsbsimFlightDynamicsBackend(JsbsimFlightDynamicsBackend&&) noexcept =
    default;

JsbsimFlightDynamicsBackend& JsbsimFlightDynamicsBackend::operator=(
    JsbsimFlightDynamicsBackend&&) noexcept = default;

const AircraftConfigurationIdentity& JsbsimFlightDynamicsBackend::aircraft() const noexcept {
  return impl_->aircraft();
}

const FlightDynamicsState& JsbsimFlightDynamicsBackend::state() const noexcept {
  return impl_->state();
}

const FlightDynamicsStepRecord& JsbsimFlightDynamicsBackend::last_step_record() const noexcept {
  return impl_->last_step_record();
}

void JsbsimFlightDynamicsBackend::reset(const FlightDynamicsInitialCondition& initial_condition) {
  impl_->reset(initial_condition);
}

FlightDynamicsStepRecord JsbsimFlightDynamicsBackend::step_fixed(
    double fixed_step_s,
    const AircraftControlInputSample& controls) {
  return impl_->step_fixed(fixed_step_s, controls);
}

} // namespace flying::core_sim
