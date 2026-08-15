#include "Weather.h"

#include <cmath>
#include <stdexcept>

namespace {

using flying::core_sim::DrydenTurbulenceSettings;
using flying::core_sim::Vector3d;
using flying::core_sim::WeatherModel;
using flying::core_sim::WeatherScenario;
using flying::core_sim::deterministic_dryden_turbulence;
using flying::core_sim::sample_weather_atmosphere;

void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void require_near(double actual, double expected, double tolerance, const char* message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message);
  }
}

double magnitude(Vector3d value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

void isa_1976_reference_cases_match() {
  const auto sea_level = sample_weather_atmosphere(0.0, 101'325.0, 288.15, 0.0);
  require_near(sea_level.static_pressure_pa, 101'325.0, 0.5, "ISA sea-level pressure mismatch");
  require_near(sea_level.temperature_k, 288.15, 0.001, "ISA sea-level temperature mismatch");
  require_near(sea_level.density_kgpm3, 1.225, 0.001, "ISA sea-level dry density mismatch");

  const auto five_km = sample_weather_atmosphere(5'000.0, 101'325.0, 288.15, 0.0);
  require_near(five_km.static_pressure_pa, 54'020.5, 4.0, "ISA 5 km pressure mismatch");
  require_near(five_km.temperature_k, 255.65, 0.001, "ISA 5 km temperature mismatch");
  require_near(five_km.density_kgpm3, 0.736, 0.002, "ISA 5 km dry density mismatch");

  const auto eleven_km = sample_weather_atmosphere(11'000.0, 101'325.0, 288.15, 0.0);
  require_near(eleven_km.static_pressure_pa, 22'632.1, 4.0, "ISA 11 km pressure mismatch");
  require_near(eleven_km.temperature_k, 216.65, 0.001, "ISA 11 km temperature mismatch");
  require_near(eleven_km.density_kgpm3, 0.364, 0.002, "ISA 11 km dry density mismatch");
}

void humidity_changes_density_and_dew_point() {
  const auto dry = sample_weather_atmosphere(0.0, 101'325.0, 303.15, 0.0);
  const auto humid = sample_weather_atmosphere(0.0, 101'325.0, 303.15, 1.0);

  require(humid.water_vapor_pressure_pa > 4'000.0,
          "humid atmosphere must expose water vapor pressure");
  require(humid.density_kgpm3 < dry.density_kgpm3,
          "humid air density must be lower than dry air at the same pressure and temperature");
  require_near(humid.dew_point_k, humid.temperature_k, 0.1,
               "saturated air dew point must match ambient temperature");
}

void seeded_dryden_turbulence_is_deterministic() {
  const DrydenTurbulenceSettings settings{4.0, 225.0, 21'337u};
  const Vector3d first = deterministic_dryden_turbulence(settings, 640.0, 38.0, 81.25);
  const Vector3d second = deterministic_dryden_turbulence(settings, 640.0, 38.0, 81.25);
  const Vector3d other = deterministic_dryden_turbulence({4.0, 225.0, 21'338u}, 640.0, 38.0, 81.25);

  require_near(first.x, second.x, 0.0, "seeded turbulence north component changed");
  require_near(first.y, second.y, 0.0, "seeded turbulence east component changed");
  require_near(first.z, second.z, 0.0, "seeded turbulence down component changed");
  require(magnitude(first - other) > 0.01, "turbulence seed must affect generated gust state");
}

void numerical_weather_is_independent_of_visual_clouds() {
  WeatherScenario scenario;
  scenario.scenario_id = "manual.reference";
  scenario.surface_wind.wind_ned_mps = {4.0, 1.0, 0.0};
  scenario.wind_aloft = {1'000.0, {10.0, -2.0, 0.0}};
  scenario.turbulence = {1.5, 180.0, 9001u};
  scenario.cloud = {600.0, 1'200.0, 0.75};
  scenario.precipitation = {2.0, 0.0, 0.25};
  scenario.thermal = {2.0, 49.2, 16.6, 800.0, 1'400.0};
  scenario.orographic = {{0.1, 0.0, 0.0}, 0.2};

  const WeatherModel model{scenario};
  const auto sample = model.sample(49.2, 16.6, 700.0, 12.0);
  require(sample.atmosphere.static_pressure_pa > 0.0,
          "weather model must expose numerical pressure without visual rendering");
  require(sample.wind_ned_mps.z < -0.1,
          "thermal and orographic lift must feed the numerical wind field");
  require(sample.surface_wetness_norm > 0.25,
          "precipitation must feed numerical wet-surface state");
}

} // namespace

int main() {
  isa_1976_reference_cases_match();
  humidity_changes_density_and_dew_point();
  seeded_dryden_turbulence_is_deterministic();
  numerical_weather_is_independent_of_visual_clouds();
  return 0;
}
