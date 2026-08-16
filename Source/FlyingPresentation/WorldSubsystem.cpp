#include "WorldSubsystem.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace flying::presentation {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6'371'000.0;

[[nodiscard]] double clamp(double value, double minimum, double maximum) noexcept {
  return std::max(minimum, std::min(maximum, value));
}

[[nodiscard]] double deg_to_rad(double degrees) noexcept {
  return degrees * kPi / 180.0;
}

[[nodiscard]] double rad_to_deg(double radians) noexcept {
  return radians * 180.0 / kPi;
}

[[nodiscard]] double normalize_degrees(double degrees) noexcept {
  double normalized = std::fmod(degrees, 360.0);
  if (normalized < 0.0) {
    normalized += 360.0;
  }
  return normalized;
}

[[nodiscard]] bool is_leap_year(int year) noexcept {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

[[nodiscard]] int day_of_year(int year, int month, int day) {
  static constexpr int kDaysBeforeMonth[] =
      {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  if (month < 1 || month > 12 || day < 1 || day > 31) {
    throw std::invalid_argument("invalid UTC date for sun position");
  }
  int ordinal = kDaysBeforeMonth[month - 1] + day;
  if (month > 2 && is_leap_year(year)) {
    ++ordinal;
  }
  return ordinal;
}

[[nodiscard]] double haversine_m(double lat_a_deg,
                                 double lon_a_deg,
                                 double lat_b_deg,
                                 double lon_b_deg) noexcept {
  const double lat_a = deg_to_rad(lat_a_deg);
  const double lat_b = deg_to_rad(lat_b_deg);
  const double d_lat = deg_to_rad(lat_b_deg - lat_a_deg);
  const double d_lon = deg_to_rad(lon_b_deg - lon_a_deg);
  const double a = std::sin(d_lat * 0.5) * std::sin(d_lat * 0.5) +
                   std::cos(lat_a) * std::cos(lat_b) *
                       std::sin(d_lon * 0.5) * std::sin(d_lon * 0.5);
  return kEarthRadiusM * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("unable to open world procedural rules file");
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] bool contains(std::string_view text, std::string_view token) noexcept {
  return text.find(token) != std::string_view::npos;
}

} // namespace

[[nodiscard]] SunPresentationState compute_sun_position(GeodeticLocation location,
                                                        UtcDateTime utc) {
  const int ordinal = day_of_year(utc.year, utc.month, utc.day);
  const double decimal_hour =
      static_cast<double>(utc.hour) + static_cast<double>(utc.minute) / 60.0 +
      utc.second / 3600.0;
  const double gamma =
      2.0 * kPi / 365.0 *
      (static_cast<double>(ordinal) - 1.0 + (decimal_hour - 12.0) / 24.0);

  const double equation_of_time_min =
      229.18 * (0.000075 + 0.001868 * std::cos(gamma) -
                0.032077 * std::sin(gamma) - 0.014615 * std::cos(2.0 * gamma) -
                0.040849 * std::sin(2.0 * gamma));
  const double declination_rad =
      0.006918 - 0.399912 * std::cos(gamma) + 0.070257 * std::sin(gamma) -
      0.006758 * std::cos(2.0 * gamma) + 0.000907 * std::sin(2.0 * gamma) -
      0.002697 * std::cos(3.0 * gamma) + 0.00148 * std::sin(3.0 * gamma);

  const double minutes_utc =
      static_cast<double>(utc.hour * 60 + utc.minute) + utc.second / 60.0;
  const double true_solar_time_min =
      std::fmod(minutes_utc + equation_of_time_min + 4.0 * location.longitude_deg, 1440.0);
  const double hour_angle_deg =
      true_solar_time_min / 4.0 < 0.0 ? true_solar_time_min / 4.0 + 180.0
                                      : true_solar_time_min / 4.0 - 180.0;

  const double latitude_rad = deg_to_rad(location.latitude_deg);
  const double hour_angle_rad = deg_to_rad(hour_angle_deg);
  const double cos_zenith =
      std::sin(latitude_rad) * std::sin(declination_rad) +
      std::cos(latitude_rad) * std::cos(declination_rad) * std::cos(hour_angle_rad);
  const double zenith_rad = std::acos(clamp(cos_zenith, -1.0, 1.0));
  const double elevation_deg = 90.0 - rad_to_deg(zenith_rad);

  const double azimuth_rad =
      std::atan2(std::sin(hour_angle_rad),
                 std::cos(hour_angle_rad) * std::sin(latitude_rad) -
                     std::tan(declination_rad) * std::cos(latitude_rad));
  const double azimuth_deg = normalize_degrees(rad_to_deg(azimuth_rad) + 180.0);

  return {azimuth_deg, elevation_deg, clamp((elevation_deg + 6.0) / 18.0, 0.0, 1.0)};
}

[[nodiscard]] WeatherVisualState derive_weather_visuals(
    const core_sim::WeatherSample& weather) {
  WeatherVisualState visuals{};
  visuals.source = weather.source;
  visuals.cloud_coverage_norm = clamp(weather.cloud_coverage_norm, 0.0, 1.0);
  visuals.cloud_opacity_norm =
      clamp(0.25 + visuals.cloud_coverage_norm * 0.75, 0.0, 1.0);
  visuals.precipitation_rate_mmph = std::max(0.0, weather.precipitation_rate_mmph);
  visuals.rain_alpha_norm = clamp(visuals.precipitation_rate_mmph / 20.0, 0.0, 1.0);
  visuals.snow_alpha_norm =
      weather.atmosphere.temperature_k <= 273.15 ? visuals.rain_alpha_norm : 0.0;
  visuals.visibility_m = weather.visibility_m;
  visuals.fog_density_norm = clamp((30'000.0 - weather.visibility_m) / 30'000.0, 0.0, 1.0);
  visuals.wet_surface_norm = clamp(weather.surface_wetness_norm, 0.0, 1.0);
  visuals.runway_reflection_norm =
      clamp(visuals.wet_surface_norm * (1.0 + visuals.rain_alpha_norm) * 0.5, 0.0, 1.0);
  visuals.icing_visual_norm = clamp(weather.icing_severity_norm, 0.0, 1.0);
  return visuals;
}

[[nodiscard]] WorldObjectState evaluate_object_collision(WorldObjectState object,
                                                         GeodeticLocation aircraft,
                                                         double active_safety_zone_radius_m) {
  object.distance_to_aircraft_m = haversine_m(aircraft.latitude_deg,
                                              aircraft.longitude_deg,
                                              object.location.latitude_deg,
                                              object.location.longitude_deg);
  const double effective_radius = std::min(active_safety_zone_radius_m, object.safety_radius_m);
  object.collision_enabled = object.critical_for_flight && object.collision_required &&
                             object.distance_to_aircraft_m <= effective_radius;
  return object;
}

[[nodiscard]] std::vector<WorldObjectState> evaluate_world_objects(
    std::vector<WorldObjectState> objects,
    GeodeticLocation aircraft,
    double active_safety_zone_radius_m) {
  for (auto& object : objects) {
    object = evaluate_object_collision(std::move(object), aircraft, active_safety_zone_radius_m);
  }
  return objects;
}

[[nodiscard]] WorldAudioState derive_world_audio(const AircraftWorldState& aircraft,
                                                 const core_sim::WeatherSample& weather,
                                                 const std::vector<WorldObjectState>& objects) {
  WorldAudioState audio{};
  audio.engine_active = aircraft.engine_rpm > 100.0;
  audio.engine_rpm = aircraft.engine_rpm;
  audio.engine_gain_norm =
      clamp((aircraft.engine_rpm / 2'700.0) * (0.35 + 0.65 * aircraft.throttle_norm), 0.0, 1.0);
  audio.wind_gain_norm = clamp(std::sqrt(weather.wind_ned_mps.x * weather.wind_ned_mps.x +
                                         weather.wind_ned_mps.y * weather.wind_ned_mps.y +
                                         weather.wind_ned_mps.z * weather.wind_ned_mps.z) /
                                   25.0,
                               0.0,
                               1.0);
  audio.precipitation_gain_norm = clamp(weather.precipitation_rate_mmph / 15.0, 0.0, 1.0);
  audio.rolling_gain_norm =
      aircraft.on_ground ? clamp(aircraft.ground_speed_mps / 35.0, 0.0, 1.0) : 0.0;
  for (const auto& object : objects) {
    if (!object.audio_hook.empty() && object.distance_to_aircraft_m <= object.safety_radius_m) {
      audio.environmental_hooks.push_back(object.audio_hook);
    }
  }
  return audio;
}

[[nodiscard]] ProceduralWorldRules load_world_procedural_rules(
    const std::filesystem::path& path) {
  const std::string text = read_text_file(path);
  ProceduralWorldRules rules{};
  rules.offline_only = contains(text, "\"offlineOnly\": true");
  rules.day_night_cycle = contains(text, "\"dayNightCycle\"");
  rules.weather_visuals_from_core_sim =
      contains(text, "\"weatherStateSource\": \"CoreSim\"");
  rules.active_zone_collision_only =
      contains(text, "\"collisionPolicy\": \"active_safety_zone_only\"");

  constexpr std::string_view kLayers[] = {"buildings",
                                          "vegetation",
                                          "water",
                                          "critical_obstacles",
                                          "wires",
                                          "runway_objects",
                                          "windsocks"};
  for (const auto layer : kLayers) {
    if (contains(text, "\"" + std::string(layer) + "\"")) {
      rules.object_layers.emplace_back(layer);
    }
  }

  constexpr std::string_view kHooks[] = {"engine_state",
                                         "wheel_roll",
                                         "wind",
                                         "precipitation",
                                         "vegetation_wind",
                                         "water_ambience",
                                         "airport_windsock_wind"};
  for (const auto hook : kHooks) {
    if (contains(text, "\"" + std::string(hook) + "\"")) {
      rules.audio_hooks.emplace_back(hook);
    }
  }

  if (!rules.offline_only || !rules.day_night_cycle ||
      !rules.weather_visuals_from_core_sim || !rules.active_zone_collision_only) {
    throw std::runtime_error("world procedural rules do not satisfy presentation invariants");
  }
  return rules;
}

} // namespace flying::presentation
