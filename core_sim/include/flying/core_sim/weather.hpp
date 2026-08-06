#pragma once

#include "flying/core_sim/math.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace flying::core_sim {

struct AtmosphereSample {
  double static_pressure_pa{101'325.0};
  double temperature_k{288.15};
  double density_kgpm3{1.225};
  double speed_of_sound_mps{340.294};
  double relative_humidity_norm{0.50};
};

enum class WeatherSource {
  Manual,
  Metar,
  Grib,
  Unavailable,
};

struct WindLayer {
  double altitude_m{};
  Vector3d wind_ned_mps{};
};

struct DrydenTurbulenceSettings {
  double intensity_mps{};
  double scale_length_m{200.0};
  std::uint32_t seed{1u};
};

struct CloudLayer {
  double base_altitude_m{1'200.0};
  double top_altitude_m{2'000.0};
  double coverage_norm{};
};

struct PrecipitationState {
  double rain_rate_mmph{};
  double snow_rate_mmph{};
  double surface_wetness_norm{};
};

struct WeatherScenario {
  std::string scenario_id{"manual.cavok"};
  WeatherSource source{WeatherSource::Manual};
  double qnh_pa{101'325.0};
  double sea_level_temperature_k{288.15};
  double relative_humidity_norm{0.50};
  double visibility_m{30'000.0};
  WindLayer surface_wind{};
  WindLayer wind_aloft{1'000.0, {}};
  DrydenTurbulenceSettings turbulence{};
  CloudLayer cloud{};
  PrecipitationState precipitation{};
  double icing_severity_norm{};
};

struct WeatherSample {
  WeatherSource source{WeatherSource::Manual};
  AtmosphereSample atmosphere{};
  Vector3d steady_wind_ned_mps{};
  Vector3d gust_ned_mps{};
  Vector3d turbulence_ned_mps{};
  Vector3d wind_ned_mps{};
  double visibility_m{30'000.0};
  double cloud_coverage_norm{};
  double precipitation_rate_mmph{};
  double surface_wetness_norm{};
  double icing_severity_norm{};
  double runway_friction_scale{1.0};
};

class IWeatherAdapter {
public:
  virtual ~IWeatherAdapter() = default;

  [[nodiscard]] virtual bool enabled() const noexcept = 0;
  [[nodiscard]] virtual WeatherSource source() const noexcept = 0;
  [[nodiscard]] virtual std::optional<WeatherScenario> load_for_location(
      double latitude_deg,
      double longitude_deg,
      double unix_time_s) = 0;
};

class DisabledWeatherAdapter final : public IWeatherAdapter {
public:
  [[nodiscard]] bool enabled() const noexcept override;
  [[nodiscard]] WeatherSource source() const noexcept override;
  [[nodiscard]] std::optional<WeatherScenario> load_for_location(
      double latitude_deg,
      double longitude_deg,
      double unix_time_s) override;
};

class WeatherModel {
public:
  explicit WeatherModel(WeatherScenario scenario = {});

  [[nodiscard]] const WeatherScenario& scenario() const noexcept;
  void set_manual_scenario(WeatherScenario scenario);
  [[nodiscard]] WeatherSample sample(double latitude_deg,
                                     double longitude_deg,
                                     double altitude_m,
                                     double simulation_time_s) const;

private:
  WeatherScenario scenario_{};
};

[[nodiscard]] std::string_view to_string(WeatherSource value) noexcept;
[[nodiscard]] WeatherScenario make_cavok_weather_scenario() noexcept;
[[nodiscard]] WeatherSample sample_weather(const WeatherScenario& scenario,
                                           double latitude_deg,
                                           double longitude_deg,
                                           double altitude_m,
                                           double simulation_time_s);
[[nodiscard]] AtmosphereSample sample_weather_atmosphere(double altitude_m,
                                                         double qnh_pa,
                                                         double sea_level_temperature_k,
                                                         double relative_humidity_norm);
[[nodiscard]] Vector3d deterministic_dryden_turbulence(
    DrydenTurbulenceSettings settings,
    double altitude_m,
    double true_airspeed_mps,
    double simulation_time_s) noexcept;
[[nodiscard]] double pressure_altitude_from_static_pressure(double static_pressure_pa,
                                                           double altimeter_setting_pa);
[[nodiscard]] double qfe_pressure_for_field(double field_elevation_m, double qnh_pa);

} // namespace flying::core_sim
