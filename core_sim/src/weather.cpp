#include "flying/core_sim/weather.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace flying::core_sim {
namespace {

constexpr double kSeaLevelPressurePa = 101'325.0;
constexpr double kSeaLevelTemperatureK = 288.15;
constexpr double kLapseRateKpm = 0.0065;
constexpr double kGasConstantAir = 287.05287;
constexpr double kGammaAir = 1.4;
constexpr double kPressureExponent = 5.2558797;
constexpr double kMinPressurePa = 1'000.0;

[[nodiscard]] double clamp(double value, double min_value, double max_value) noexcept {
  return std::max(min_value, std::min(max_value, value));
}

[[nodiscard]] double finite_or(double value, double fallback) noexcept {
  return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] double smootherstep(double value) noexcept {
  value = clamp(value, 0.0, 1.0);
  return value * value * value * (value * (value * 6.0 - 15.0) + 10.0);
}

[[nodiscard]] double hash_unit(std::uint32_t seed, std::int64_t index, std::uint32_t channel) noexcept {
  std::uint64_t x = static_cast<std::uint64_t>(seed) + 0x9e37'79b9'7f4a'7c15ull;
  x ^= static_cast<std::uint64_t>(index) + channel * 0xbf58'476d'1ce4'e5b9ull;
  x ^= x >> 30u;
  x *= 0xbf58'476d'1ce4'e5b9ull;
  x ^= x >> 27u;
  x *= 0x94d0'49bb'1331'11ebull;
  x ^= x >> 31u;
  const double unit = static_cast<double>(x >> 11u) * (1.0 / 9'007'199'254'740'992.0);
  return unit * 2.0 - 1.0;
}

[[nodiscard]] double value_noise(std::uint32_t seed, double x, std::uint32_t channel) noexcept {
  const auto i0 = static_cast<std::int64_t>(std::floor(x));
  const double frac = x - static_cast<double>(i0);
  const double a = hash_unit(seed, i0, channel);
  const double b = hash_unit(seed, i0 + 1, channel);
  return a + (b - a) * smootherstep(frac);
}

void validate_scenario(const WeatherScenario& scenario) {
  const auto finite = [](double value) noexcept { return std::isfinite(value); };
  if (!finite(scenario.qnh_pa) || scenario.qnh_pa < kMinPressurePa) {
    throw std::invalid_argument("weather qnh_pa must be a finite pressure >= 1000 Pa");
  }
  if (!finite(scenario.sea_level_temperature_k) || scenario.sea_level_temperature_k < 150.0) {
    throw std::invalid_argument("weather sea_level_temperature_k must be finite and physical");
  }
  if (!finite(scenario.relative_humidity_norm) ||
      scenario.relative_humidity_norm < 0.0 ||
      scenario.relative_humidity_norm > 1.0) {
    throw std::invalid_argument("weather relative_humidity_norm must be in [0, 1]");
  }
  if (!finite(scenario.visibility_m) || scenario.visibility_m < 0.0) {
    throw std::invalid_argument("weather visibility_m must be finite and non-negative");
  }
}

[[nodiscard]] Vector3d lerp(Vector3d a, Vector3d b, double t) noexcept {
  return a + (b - a) * clamp(t, 0.0, 1.0);
}

[[nodiscard]] double horizontal_magnitude(Vector3d value) noexcept {
  return std::hypot(value.x, value.y);
}

} // namespace

bool DisabledWeatherAdapter::enabled() const noexcept {
  return false;
}

WeatherSource DisabledWeatherAdapter::source() const noexcept {
  return WeatherSource::Unavailable;
}

std::optional<WeatherScenario> DisabledWeatherAdapter::load_for_location(
    double,
    double,
    double) {
  return std::nullopt;
}

WeatherModel::WeatherModel(WeatherScenario scenario)
    : scenario_(std::move(scenario)) {
  validate_scenario(scenario_);
}

const WeatherScenario& WeatherModel::scenario() const noexcept {
  return scenario_;
}

void WeatherModel::set_manual_scenario(WeatherScenario scenario) {
  scenario.source = WeatherSource::Manual;
  validate_scenario(scenario);
  scenario_ = std::move(scenario);
}

WeatherSample WeatherModel::sample(double latitude_deg,
                                   double longitude_deg,
                                   double altitude_m,
                                   double simulation_time_s) const {
  return sample_weather(scenario_, latitude_deg, longitude_deg, altitude_m, simulation_time_s);
}

std::string_view to_string(WeatherSource value) noexcept {
  switch (value) {
  case WeatherSource::Manual:
    return "manual";
  case WeatherSource::Metar:
    return "metar";
  case WeatherSource::Grib:
    return "grib";
  case WeatherSource::Unavailable:
    return "unavailable";
  }
  return "unavailable";
}

WeatherScenario make_cavok_weather_scenario() noexcept {
  return {};
}

AtmosphereSample sample_weather_atmosphere(double altitude_m,
                                           double qnh_pa,
                                           double sea_level_temperature_k,
                                           double relative_humidity_norm) {
  altitude_m = finite_or(altitude_m, 0.0);
  qnh_pa = std::max(kMinPressurePa, finite_or(qnh_pa, kSeaLevelPressurePa));
  sea_level_temperature_k =
      std::max(150.0, finite_or(sea_level_temperature_k, kSeaLevelTemperatureK));
  relative_humidity_norm = clamp(finite_or(relative_humidity_norm, 0.50), 0.0, 1.0);

  const double temperature_at_altitude =
      std::max(150.0, sea_level_temperature_k - kLapseRateKpm * altitude_m);
  const double standard_temperature_at_altitude =
      std::max(150.0, kSeaLevelTemperatureK - kLapseRateKpm * altitude_m);
  const double pressure_ratio =
      std::pow(standard_temperature_at_altitude / kSeaLevelTemperatureK, kPressureExponent);
  const double pressure = std::max(kMinPressurePa, qnh_pa * pressure_ratio);
  const double density = pressure / (kGasConstantAir * temperature_at_altitude);
  const double speed_of_sound = std::sqrt(kGammaAir * kGasConstantAir * temperature_at_altitude);
  return {pressure, temperature_at_altitude, density, speed_of_sound, relative_humidity_norm};
}

Vector3d deterministic_dryden_turbulence(DrydenTurbulenceSettings settings,
                                         double altitude_m,
                                         double true_airspeed_mps,
                                         double simulation_time_s) noexcept {
  const double intensity = std::max(0.0, finite_or(settings.intensity_mps, 0.0));
  if (intensity == 0.0) {
    return {};
  }
  const double scale = std::max(1.0, finite_or(settings.scale_length_m, 200.0));
  const double speed = std::max(1.0, finite_or(true_airspeed_mps, 1.0));
  const double altitude_gain = 0.65 + 0.35 * std::exp(-std::max(0.0, altitude_m) / 1'500.0);
  const double x = finite_or(simulation_time_s, 0.0) * speed / scale;
  return {
    value_noise(settings.seed, x, 0u) * intensity * altitude_gain,
    value_noise(settings.seed, x * 1.37 + 11.0, 1u) * intensity * altitude_gain,
    value_noise(settings.seed, x * 1.91 + 23.0, 2u) * intensity * 0.65 * altitude_gain,
  };
}

WeatherSample sample_weather(const WeatherScenario& scenario,
                             double latitude_deg,
                             double longitude_deg,
                             double altitude_m,
                             double simulation_time_s) {
  validate_scenario(scenario);
  altitude_m = finite_or(altitude_m, 0.0);
  simulation_time_s = finite_or(simulation_time_s, 0.0);

  const double layer_delta =
      scenario.wind_aloft.altitude_m - scenario.surface_wind.altitude_m;
  const double layer_t =
      std::abs(layer_delta) < 1.0
          ? 1.0
          : (altitude_m - scenario.surface_wind.altitude_m) / layer_delta;
  const Vector3d steady_wind = lerp(
      scenario.surface_wind.wind_ned_mps,
      scenario.wind_aloft.wind_ned_mps,
      layer_t);

  const double gust_phase =
      simulation_time_s * 0.05 +
      finite_or(latitude_deg, 0.0) * 0.17 +
      finite_or(longitude_deg, 0.0) * 0.11;
  const double gust_strength = scenario.turbulence.intensity_mps * 0.45;
  const Vector3d gust{
    std::sin(gust_phase) * gust_strength,
    std::sin(gust_phase * 1.7 + 1.2) * gust_strength * 0.7,
    std::sin(gust_phase * 2.3 + 0.4) * gust_strength * 0.25,
  };
  const Vector3d turbulence = deterministic_dryden_turbulence(
      scenario.turbulence,
      altitude_m,
      std::max(15.0, horizontal_magnitude(steady_wind)),
      simulation_time_s);
  const double precipitation_rate =
      std::max(0.0, scenario.precipitation.rain_rate_mmph) +
      std::max(0.0, scenario.precipitation.snow_rate_mmph);
  const double precipitation_visibility =
      precipitation_rate > 0.0 ? 18'000.0 / (1.0 + precipitation_rate * 0.18) : scenario.visibility_m;
  const double cloud_visibility =
      scenario.cloud.coverage_norm > 0.2 &&
              altitude_m >= scenario.cloud.base_altitude_m &&
              altitude_m <= scenario.cloud.top_altitude_m
          ? 1'500.0 / std::max(0.1, scenario.cloud.coverage_norm)
          : scenario.visibility_m;
  const double wetness =
      clamp(scenario.precipitation.surface_wetness_norm + precipitation_rate / 20.0, 0.0, 1.0);

  WeatherSample sample;
  sample.source = scenario.source;
  sample.atmosphere = sample_weather_atmosphere(
      altitude_m,
      scenario.qnh_pa,
      scenario.sea_level_temperature_k,
      scenario.relative_humidity_norm);
  sample.steady_wind_ned_mps = steady_wind;
  sample.gust_ned_mps = gust;
  sample.turbulence_ned_mps = turbulence;
  sample.wind_ned_mps = steady_wind + gust + turbulence;
  sample.visibility_m = std::min(scenario.visibility_m, std::min(precipitation_visibility, cloud_visibility));
  sample.cloud_coverage_norm = clamp(scenario.cloud.coverage_norm, 0.0, 1.0);
  sample.precipitation_rate_mmph = precipitation_rate;
  sample.surface_wetness_norm = wetness;
  sample.icing_severity_norm =
      clamp(scenario.icing_severity_norm +
                (sample.atmosphere.temperature_k < 276.15 &&
                 sample.atmosphere.relative_humidity_norm > 0.75
                     ? sample.cloud_coverage_norm * 0.75
                     : 0.0),
            0.0,
            1.0);
  sample.runway_friction_scale = clamp(1.0 - wetness * 0.35 - std::max(0.0, scenario.precipitation.snow_rate_mmph) * 0.03,
                                       0.45,
                                       1.0);
  return sample;
}

double pressure_altitude_from_static_pressure(double static_pressure_pa,
                                             double altimeter_setting_pa) {
  static_pressure_pa = std::max(kMinPressurePa, finite_or(static_pressure_pa, kSeaLevelPressurePa));
  altimeter_setting_pa =
      std::max(kMinPressurePa, finite_or(altimeter_setting_pa, kSeaLevelPressurePa));
  const double ratio = static_pressure_pa / altimeter_setting_pa;
  return (kSeaLevelTemperatureK / kLapseRateKpm) *
         (1.0 - std::pow(ratio, 1.0 / kPressureExponent));
}

double qfe_pressure_for_field(double field_elevation_m, double qnh_pa) {
  return sample_weather_atmosphere(field_elevation_m, qnh_pa, kSeaLevelTemperatureK, 0.50)
      .static_pressure_pa;
}

} // namespace flying::core_sim
