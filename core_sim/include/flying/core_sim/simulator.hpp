#pragma once

#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/fixed_step.hpp"
#include "flying/core_sim/input.hpp"
#include "flying/core_sim/state.hpp"

namespace flying::core_sim {

struct AdvanceReport {
  std::uint32_t steps_executed{};
  double fixed_step_s{};
  double consumed_time_s{};
  double remaining_accumulator_s{};
  std::uint64_t total_steps{};
  std::uint64_t state_hash{};
};

class CoreSimulator {
public:
  explicit CoreSimulator(RigidBodyParameters parameters = {});

  [[nodiscard]] const AuthoritativeState& state() const noexcept;
  [[nodiscard]] const FlightDynamicsInitialCondition& flight_dynamics_initial_condition()
      const noexcept;
  [[nodiscard]] const AircraftControlInputSample& initial_aircraft_controls() const noexcept;
  [[nodiscard]] const RigidBodyParameters& parameters() const noexcept;
  [[nodiscard]] const AircraftMassBalanceState& aircraft_mass_balance() const noexcept;
  [[nodiscard]] double fixed_step_s() const noexcept;

  void reset(AuthoritativeState state = {}) noexcept;
  void reset(AuthoritativeState state,
             const FlightDynamicsInitialCondition& flight_dynamics_initial_condition,
             const AircraftControlInputSample& initial_aircraft_controls);
  void set_aircraft_mass_balance(const AircraftMassBalanceState& mass_balance);
  AdvanceReport advance(double caller_delta_s, const ControlInputSample& input);
  void integrate_fixed_step(const ControlInputSample& input);

private:
  FixedStepAccumulator accumulator_;
  RigidBodyParameters parameters_;
  AuthoritativeState state_;
  FlightDynamicsInitialCondition flight_dynamics_initial_condition_{};
  AircraftControlInputSample initial_aircraft_controls_{};
};

} // namespace flying::core_sim
