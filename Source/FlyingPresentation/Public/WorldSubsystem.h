#pragma once

#include "Weather.h"

#include <filesystem>
#include <string>
#include <vector>

namespace flying::presentation {

struct GeodeticLocation {
  double latitude_deg{};
  double longitude_deg{};
  double altitude_m{};
};

struct UtcDateTime {
  int year{2026};
  int month{1};
  int day{1};
  int hour{};
  int minute{};
  double second{};
};

struct SunPresentationState {
  double azimuth_deg{};
  double elevation_deg{};
  double daylight_norm{};
};

struct WeatherVisualState {
  core_sim::WeatherSource source{core_sim::WeatherSource::Manual};
  double cloud_coverage_norm{};
  double cloud_opacity_norm{};
  double precipitation_rate_mmph{};
  double rain_alpha_norm{};
  double snow_alpha_norm{};
  double visibility_m{30'000.0};
  double fog_density_norm{};
  double wet_surface_norm{};
  double runway_reflection_norm{};
  double icing_visual_norm{};
};

struct WorldObjectState {
  std::string id;
  std::string kind;
  GeodeticLocation location{};
  bool critical_for_flight{};
  bool collision_required{};
  double safety_radius_m{250.0};
  bool collision_enabled{};
  double distance_to_aircraft_m{};
  std::string audio_hook;
};

struct AircraftWorldState {
  GeodeticLocation location{};
  double engine_rpm{};
  double throttle_norm{};
  double ground_speed_mps{};
  bool on_ground{};
};

struct WorldAudioState {
  bool engine_active{};
  double engine_rpm{};
  double engine_gain_norm{};
  double wind_gain_norm{};
  double precipitation_gain_norm{};
  double rolling_gain_norm{};
  std::vector<std::string> environmental_hooks;
};

struct ProceduralWorldRules {
  bool offline_only{};
  bool day_night_cycle{};
  bool weather_visuals_from_core_sim{};
  bool active_zone_collision_only{};
  std::vector<std::string> object_layers;
  std::vector<std::string> audio_hooks;
};

[[nodiscard]] SunPresentationState compute_sun_position(GeodeticLocation location,
                                                        UtcDateTime utc);
[[nodiscard]] WeatherVisualState derive_weather_visuals(
    const core_sim::WeatherSample& weather);
[[nodiscard]] WorldObjectState evaluate_object_collision(
    WorldObjectState object,
    GeodeticLocation aircraft,
    double active_safety_zone_radius_m);
[[nodiscard]] std::vector<WorldObjectState> evaluate_world_objects(
    std::vector<WorldObjectState> objects,
    GeodeticLocation aircraft,
    double active_safety_zone_radius_m);
[[nodiscard]] WorldAudioState derive_world_audio(
    const AircraftWorldState& aircraft,
    const core_sim::WeatherSample& weather,
    const std::vector<WorldObjectState>& objects);
[[nodiscard]] ProceduralWorldRules load_world_procedural_rules(
    const std::filesystem::path& path);

} // namespace flying::presentation
