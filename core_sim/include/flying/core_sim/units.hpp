#pragma once

namespace flying::core_sim {

inline constexpr double kFixedStepFrequencyHz = 240.0;
inline constexpr double kFixedStepSeconds = 1.0 / kFixedStepFrequencyHz;

struct SiUnitPolicy {
  static constexpr const char* position = "meter";
  static constexpr const char* velocity = "meter_per_second";
  static constexpr const char* acceleration = "meter_per_second_squared";
  static constexpr const char* rotation = "unit_quaternion";
  static constexpr const char* angular_velocity = "radian_per_second";
  static constexpr const char* force = "newton";
  static constexpr const char* moment = "newton_meter";
  static constexpr const char* mass = "kilogram";
  static constexpr const char* inertia = "kilogram_meter_squared";
  static constexpr const char* time = "second";
};

} // namespace flying::core_sim
