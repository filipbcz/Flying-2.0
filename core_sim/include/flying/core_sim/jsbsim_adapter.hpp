#pragma once

#include "flying/core_sim/flight_dynamics.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace flying::core_sim {

struct JsbsimAircraftConfig {
  std::filesystem::path root_dir;
  std::filesystem::path aircraft_path{"aircraft"};
  std::filesystem::path engine_path{"engine"};
  std::filesystem::path systems_path{"systems"};
  std::string model_name{"flying_placeholder"};
  std::string model_version{"infrastructure-v1"};
  std::string source_license{"Project-authored infrastructure placeholder; no fidelity claim"};
};

[[nodiscard]] JsbsimAircraftConfig placeholder_jsbsim_aircraft_config();

class JsbsimFlightDynamicsBackend final : public IFlightDynamicsBackend {
public:
  explicit JsbsimFlightDynamicsBackend(JsbsimAircraftConfig config = placeholder_jsbsim_aircraft_config());
  ~JsbsimFlightDynamicsBackend() override;

  JsbsimFlightDynamicsBackend(const JsbsimFlightDynamicsBackend&) = delete;
  JsbsimFlightDynamicsBackend& operator=(const JsbsimFlightDynamicsBackend&) = delete;
  JsbsimFlightDynamicsBackend(JsbsimFlightDynamicsBackend&&) noexcept;
  JsbsimFlightDynamicsBackend& operator=(JsbsimFlightDynamicsBackend&&) noexcept;

  [[nodiscard]] const AircraftConfigurationIdentity& aircraft() const noexcept override;
  [[nodiscard]] const FlightDynamicsState& state() const noexcept override;
  [[nodiscard]] const FlightDynamicsStepRecord& last_step_record() const noexcept override;

  void reset(const FlightDynamicsInitialCondition& initial_condition) override;
  FlightDynamicsStepRecord step_fixed(double fixed_step_s,
                                      const AircraftControlInputSample& controls) override;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace flying::core_sim
