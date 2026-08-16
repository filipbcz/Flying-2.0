#include "WorldSubsystem.h"

#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

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

std::filesystem::path repo_path(const char* relative_path) {
  return std::filesystem::path(FLYING_REPO_SOURCE_DIR) / relative_path;
}

void sun_position_matches_reference_cases() {
  const auto equinox_noon = flying::presentation::compute_sun_position(
      {0.0, 0.0, 0.0}, {2026, 3, 20, 12, 0, 0.0});
  require_near(equinox_noon.elevation_deg,
               88.0,
               2.0,
               "equatorial equinox noon sun elevation should be near zenith");

  const auto prague_solstice = flying::presentation::compute_sun_position(
      {50.0755, 14.4378, 250.0}, {2026, 6, 21, 11, 0, 0.0});
  require_near(prague_solstice.elevation_deg,
               63.4,
               2.0,
               "Prague summer-solstice solar noon elevation should match reference");
  require(prague_solstice.azimuth_deg > 150.0 && prague_solstice.azimuth_deg < 210.0,
          "Prague summer-solstice solar noon should place the sun to the south");
  require(prague_solstice.daylight_norm > 0.95,
          "high summer sun should produce full daylight presentation intensity");

  const auto prague_midnight = flying::presentation::compute_sun_position(
      {50.0755, 14.4378, 250.0}, {2026, 12, 21, 0, 0, 0.0});
  require(prague_midnight.elevation_deg < -55.0,
          "Prague winter-solstice midnight sun should be well below the horizon");
  require_near(prague_midnight.daylight_norm,
               0.0,
               0.0,
               "night sun position should produce zero daylight intensity");
}

void weather_visuals_share_core_sim_weather_state() {
  flying::core_sim::WeatherScenario scenario{};
  scenario.source = flying::core_sim::WeatherSource::Manual;
  scenario.qnh_pa = 100'500.0;
  scenario.sea_level_temperature_k = 276.15;
  scenario.relative_humidity_norm = 0.92;
  scenario.visibility_m = 4'500.0;
  scenario.surface_wind.wind_ned_mps = {8.0, -3.0, 0.0};
  scenario.cloud.coverage_norm = 0.85;
  scenario.precipitation.rain_rate_mmph = 6.0;
  scenario.precipitation.surface_wetness_norm = 0.75;
  scenario.icing_severity_norm = 0.25;

  const auto weather = flying::core_sim::sample_weather(scenario, 49.2, 16.6, 420.0, 30.0);
  const auto visuals = flying::presentation::derive_weather_visuals(weather);

  require(visuals.source == weather.source,
          "weather visuals must preserve the CoreSim weather source");
  require_near(visuals.cloud_coverage_norm,
               weather.cloud_coverage_norm,
               1.0e-12,
               "cloud rendering must use CoreSim cloud coverage");
  require_near(visuals.precipitation_rate_mmph,
               weather.precipitation_rate_mmph,
               1.0e-12,
               "precipitation visuals must use CoreSim precipitation rate");
  require_near(visuals.visibility_m,
               weather.visibility_m,
               1.0e-12,
               "visibility visuals must use CoreSim visibility");
  require_near(visuals.wet_surface_norm,
               weather.surface_wetness_norm,
               1.0e-12,
               "wet surfaces must use CoreSim wetness state");
  require_near(visuals.icing_visual_norm,
               weather.icing_severity_norm,
               1.0e-12,
               "icing visuals must use CoreSim icing state");
  require(visuals.fog_density_norm > 0.0,
          "reduced CoreSim visibility should produce non-zero fog density");
}

void critical_object_collision_is_limited_to_active_safety_zone() {
  const flying::presentation::GeodeticLocation aircraft{50.1000, 14.2500, 320.0};
  std::vector<flying::presentation::WorldObjectState> objects{
      {"near-wire",
       "wire",
       {50.1009, 14.2500, 335.0},
       true,
       true,
       200.0,
       false,
       0.0,
       "vegetation_wind"},
      {"far-mast",
       "critical_obstacle",
       {50.1200, 14.2500, 420.0},
       true,
       true,
       200.0,
       false,
       0.0,
       "airport_windsock_wind"},
      {"near-building",
       "building",
       {50.1005, 14.2500, 330.0},
       false,
       true,
       200.0,
       false,
       0.0,
       ""},
      {"oversized-safety-radius-mast",
       "critical_obstacle",
       {50.1090, 14.2500, 420.0},
       true,
       true,
       2'000.0,
       false,
       0.0,
       ""},
  };

  const auto evaluated =
      flying::presentation::evaluate_world_objects(std::move(objects), aircraft, 750.0);
  require(evaluated[0].collision_enabled,
          "critical nearby wire should enable collision inside active safety zone");
  require(!evaluated[1].collision_enabled,
          "critical distant obstacle should not enable collision outside active safety zone");
  require(!evaluated[2].collision_enabled,
          "non-critical procedural buildings should not enable flight-object collision");
  require(!evaluated[3].collision_enabled,
          "object safety radius must not expand collision beyond the active safety zone");
}

void world_audio_is_driven_by_aircraft_and_weather_state() {
  flying::core_sim::WeatherSample weather{};
  weather.wind_ned_mps = {10.0, 0.0, 0.0};
  weather.precipitation_rate_mmph = 3.0;

  const flying::presentation::AircraftWorldState aircraft{
      {50.1000, 14.2500, 320.0}, 2'150.0, 0.70, 18.0, true};
  const std::vector<flying::presentation::WorldObjectState> objects{
      {"windsock",
       "windsock",
       {50.1001, 14.2500, 320.0},
       true,
       false,
       100.0,
       false,
       8.0,
       "airport_windsock_wind"}};

  const auto audio = flying::presentation::derive_world_audio(aircraft, weather, objects);
  require(audio.engine_active, "engine audio should activate from engine rpm");
  require_near(audio.engine_rpm, aircraft.engine_rpm, 1.0e-12, "audio rpm must follow aircraft state");
  require(audio.engine_gain_norm > 0.0, "engine gain should reflect throttle and rpm");
  require(audio.wind_gain_norm > 0.0, "wind audio should reflect CoreSim wind");
  require(audio.precipitation_gain_norm > 0.0,
          "precipitation audio should reflect CoreSim precipitation");
  require(audio.rolling_gain_norm > 0.0, "ground roll audio should reflect ground speed");
  require(!audio.environmental_hooks.empty(),
          "nearby world-object audio hooks should be emitted");
}

void procedural_rules_cover_required_world_layers() {
  const auto rules = flying::presentation::load_world_procedural_rules(
      repo_path("Content/WorldProceduralRules/WorldRules.json"));
  require(rules.offline_only, "world procedural rules must be offline-only");
  require(rules.day_night_cycle, "world procedural rules must enable day/night rendering");
  require(rules.weather_visuals_from_core_sim,
          "world procedural rules must source weather visuals from CoreSim");
  require(rules.active_zone_collision_only,
          "world procedural rules must constrain critical collision to the active safety zone");
  require(rules.object_layers.size() == 7,
          "world procedural rules must cover buildings, vegetation, water, obstacles, wires, runway objects, and windsocks");
  require(rules.audio_hooks.size() >= 7,
          "world procedural rules must cover simulator-state and environment audio hooks");
}

} // namespace

int main() {
  sun_position_matches_reference_cases();
  weather_visuals_share_core_sim_weather_state();
  critical_object_collision_is_limited_to_active_safety_zone();
  world_audio_is_driven_by_aircraft_and_weather_state();
  procedural_rules_cover_required_world_layers();
  return 0;
}
