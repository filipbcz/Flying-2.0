#include "flying/core_sim/determinism.hpp"
#include "flying/core_sim/scenario.hpp"
#include "flying/core_sim/telemetry.hpp"
#include "flying/geo_terrain/geodesy.hpp"
#include "flying/geo_terrain/terrain_service.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using flying::core_sim::AdvanceReport;
using flying::core_sim::AircraftControlInputSample;
using flying::core_sim::AuthoritativeState;
using flying::core_sim::ControlInputSample;
using flying::core_sim::CoreSimulator;
using flying::core_sim::EngineStateSample;
using flying::core_sim::ReplayCompatibilityPolicy;
using flying::core_sim::ReplayEnvironment;
using flying::core_sim::RigidBodyParameters;
using flying::core_sim::ScenarioSelection;
using flying::core_sim::ScenarioStartMode;
using flying::core_sim::TelemetryRecorder;
using flying::geo_terrain::EcefPosition;
using flying::geo_terrain::EcefVector;
using flying::geo_terrain::EnuVector;
using flying::geo_terrain::InMemoryTerrainHeightService;
using flying::geo_terrain::OrthometricHeight;
using flying::geo_terrain::TerrainCollisionMetadata;
using flying::geo_terrain::TerrainConfidenceMetadata;
using flying::geo_terrain::TerrainHeightSample;
using flying::geo_terrain::TerrainLocalBounds;
using flying::geo_terrain::TerrainSourceAuthority;
using flying::geo_terrain::TerrainSurfaceMaterial;
using flying::geo_terrain::make_geodetic_degrees;
using flying::geo_terrain::make_sloped_terrain_plane;

constexpr double kFrameDeltaS = 1.0 / 60.0;
constexpr double kRunwayOriginElevationM = 429.2;
constexpr double kExpectedCoreSimStepsPerFrame = 4.0;

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

double clamp(double value, double min_value, double max_value) {
  return std::max(min_value, std::min(value, max_value));
}

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_text(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  output << content;
}

std::filesystem::path make_temp_root(std::string_view name) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         (std::string{name} + "-" + std::to_string(stamp));
}

enum class FlightPhase : std::size_t {
  Taxi = 0,
  TakeoffRoll,
  ClimbOut,
  Crosswind,
  Downwind,
  Base,
  Final,
  LandingRoll,
  Stop,
  Count,
};

struct PhaseSegment {
  FlightPhase phase{};
  const char* name{};
  std::uint32_t frames{};
  double start_east_m{};
  double start_north_m{};
  double end_east_m{};
  double end_north_m{};
  double start_agl_m{};
  double end_agl_m{};
  double target_forward_speed_mps{};
  double throttle_norm{};
  double flaps_norm{};
  double brake_norm{};
  bool runway_contact_expected{};
};

struct FlightMetrics {
  std::array<bool, static_cast<std::size_t>(FlightPhase::Count)> phase_seen{};
  std::uint64_t loading_screen_frames{};
  std::uint64_t core_sim_step_misses{};
  double max_agl_m{};
  double final_agl_m{};
  double final_forward_speed_mps{};
  std::vector<double> frame_times_ms;
  std::vector<double> input_latency_ms;
  double peak_ram_mib{};
  double peak_vram_mib{};
};

struct PerformanceCapture {
  double average_fps{};
  double one_percent_low_fps{};
  std::uint64_t core_sim_step_misses{};
  double p95_input_latency_ms{};
  std::uint64_t hitch_count{};
  double peak_ram_mib{};
  double peak_vram_mib{};
};

std::vector<PhaseSegment> make_m1_flight_plan() {
  return {
    {FlightPhase::Taxi, "taxi", 120, -80.0, -20.0, 0.0, 0.0, 1.5, 1.5, 5.0, 0.18, 0.0, 0.0, true},
    {FlightPhase::TakeoffRoll, "takeoff_roll", 420, 0.0, 0.0, 650.0, 0.0, 1.5, 18.0, 34.0, 0.95, 0.15, 0.0, true},
    {FlightPhase::ClimbOut, "climb_out", 480, 650.0, 0.0, 1'800.0, 0.0, 18.0, 230.0, 42.0, 0.82, 0.10, 0.0, false},
    {FlightPhase::Crosswind, "crosswind", 420, 1'800.0, 0.0, 1'800.0, 850.0, 230.0, 300.0, 42.0, 0.70, 0.0, 0.0, false},
    {FlightPhase::Downwind, "downwind", 600, 1'800.0, 850.0, -250.0, 850.0, 300.0, 300.0, 38.0, 0.64, 0.0, 0.0, false},
    {FlightPhase::Base, "base", 420, -250.0, 850.0, -650.0, 220.0, 300.0, 170.0, 32.0, 0.52, 0.15, 0.0, false},
    {FlightPhase::Final, "final", 480, -650.0, 220.0, -80.0, 0.0, 170.0, 5.0, 24.0, 0.38, 0.30, 0.0, false},
    {FlightPhase::LandingRoll, "landing_roll", 360, -80.0, 0.0, 500.0, 0.0, 5.0, 1.5, 7.0, 0.12, 0.30, 0.55, true},
    {FlightPhase::Stop, "stop", 180, 500.0, 0.0, 520.0, 0.0, 1.5, 1.5, 0.0, 0.0, 0.0, 1.0, true},
  };
}

InMemoryTerrainHeightService make_pilot_terrain_service() {
  InMemoryTerrainHeightService service{
    make_geodetic_degrees(49.2, 14.4942, {kRunwayOriginElevationM})};

  const auto terrain_plane =
      make_sloped_terrain_plane(OrthometricHeight{kRunwayOriginElevationM},
                                0.0,
                                0.0,
                                0.00035,
                                -0.00020);
  service.add_dem_surface({
    TerrainLocalBounds{-6'000.0, 0.0, -3'000.0, 3'000.0},
    terrain_plane,
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, true, 0.04, "pilot-dem-west-collision"},
    "dmr5g-pilot-west",
    TerrainConfidenceMetadata{0.96, 0.25, 1.0, true},
    100,
    "pilot-dem-west",
  });
  service.add_dem_surface({
    TerrainLocalBounds{0.0, 6'000.0, -3'000.0, 3'000.0},
    terrain_plane,
    TerrainSurfaceMaterial::kGrass,
    TerrainCollisionMetadata{true, true, 0.04, "pilot-dem-east-collision"},
    "dmr5g-pilot-east",
    TerrainConfidenceMetadata{0.96, 0.25, 1.0, true},
    100,
    "pilot-dem-east",
  });
  service.add_runway_override({
    TerrainLocalBounds{-130.0, 950.0, -38.0, 38.0},
    terrain_plane,
    TerrainSurfaceMaterial::kAsphalt,
    TerrainCollisionMetadata{true, true, 0.0, "FPPV-RWY-09-27-collision"},
    "FPPV-RWY-09-27-runway-override",
    TerrainConfidenceMetadata{0.99, 0.05, 0.10, true},
    1'000,
    "FPPV-RWY-09-27",
  });

  return service;
}

EnuVector interpolate_position(const PhaseSegment& segment, double unit) {
  return {
    segment.start_east_m + (segment.end_east_m - segment.start_east_m) * unit,
    segment.start_north_m + (segment.end_north_m - segment.start_north_m) * unit,
    0.0,
  };
}

double interpolate_agl(const PhaseSegment& segment, double unit) {
  return segment.start_agl_m + (segment.end_agl_m - segment.start_agl_m) * unit;
}

TerrainHeightSample sample_local(const InMemoryTerrainHeightService& service, EnuVector local) {
  return service.sample(service.query_from_local_enu({local.east_m, local.north_m, 0.0}));
}

double terrain_up_m(const TerrainHeightSample& sample) {
  return sample.height.meters - kRunwayOriginElevationM;
}

EnuVector local_position_for(const InMemoryTerrainHeightService& service,
                             const AuthoritativeState& state) {
  return flying::geo_terrain::enu_from_ecef_position(
      service.local_frame(),
      EcefPosition{{state.ecef_position_m.x, state.ecef_position_m.y, state.ecef_position_m.z}});
}

EnuVector local_velocity_for(const InMemoryTerrainHeightService& service,
                             const AuthoritativeState& state) {
  return flying::geo_terrain::enu_from_ecef_vector(
      service.local_frame(),
      EcefVector{{state.ecef_velocity_mps.x, state.ecef_velocity_mps.y, state.ecef_velocity_mps.z}});
}

AircraftControlInputSample aircraft_controls_for(const PhaseSegment& segment) {
  AircraftControlInputSample controls{};
  controls.throttle_norm = segment.throttle_norm;
  controls.flaps_norm = segment.flaps_norm;
  controls.brake_left_norm = segment.brake_norm;
  controls.brake_right_norm = segment.brake_norm;
  controls.mixture_norm = segment.throttle_norm > 0.0 ? 1.0 : 0.0;
  controls.propeller_norm = segment.throttle_norm > 0.0 ? 1.0 : 0.0;
  controls.elevator_norm = segment.phase == FlightPhase::Final ? 0.08 : -0.04;
  controls.rudder_norm =
      (segment.phase == FlightPhase::Crosswind || segment.phase == FlightPhase::Base) ? 0.18 : 0.0;
  controls.aileron_norm =
      (segment.phase == FlightPhase::Crosswind || segment.phase == FlightPhase::Base) ? 0.12 : 0.0;
  return controls;
}

ControlInputSample control_input_for(const PhaseSegment& segment,
                                     double desired_agl_m,
                                     const InMemoryTerrainHeightService& terrain,
                                     const CoreSimulator& simulator) {
  const EnuVector local = local_position_for(terrain, simulator.state());
  const EnuVector velocity = local_velocity_for(terrain, simulator.state());
  const TerrainHeightSample sample = sample_local(terrain, local);
  const double actual_agl_m = local.up_m - terrain_up_m(sample);
  const double forward_force_n =
      clamp(simulator.parameters().mass_kg *
                1.9 * (segment.target_forward_speed_mps - velocity.east_m),
            -6'500.0,
            6'500.0);
  const double vertical_down_force_n =
      clamp(simulator.parameters().mass_kg *
                (1.2 * (actual_agl_m - desired_agl_m) + 2.0 * velocity.up_m),
            -7'000.0,
            7'000.0);

  return {{forward_force_n, 0.0, vertical_down_force_n}, {0.0, 0.0, 0.0}};
}

void validate_pilot_terrain_continuity(const InMemoryTerrainHeightService& service) {
  for (const double north_m : {-850.0, 0.0, 850.0}) {
    const TerrainHeightSample west = sample_local(service, {-0.05, north_m, 0.0});
    const TerrainHeightSample east = sample_local(service, {0.05, north_m, 0.0});
    require(west.collision.available && east.collision.available,
            "pilot terrain seam samples must expose collision");
    require(west.collision.watertight && east.collision.watertight,
            "pilot terrain seam samples must be watertight");
    require_near(west.height.meters, east.height.meters, 0.002,
                 "adjacent pilot terrain tiles must be height-continuous at the route seam");
  }

  for (const PhaseSegment& segment : make_m1_flight_plan()) {
    for (const double unit : {0.0, 0.5, 1.0}) {
      const TerrainHeightSample sample = sample_local(service, interpolate_position(segment, unit));
      require(sample.source_authority != TerrainSourceAuthority::kUnavailable,
              "scripted flight route must stay inside loaded pilot terrain");
      require(sample.collision.available, "scripted route terrain must expose collision metadata");
      require(sample.confidence.validated, "scripted route terrain must use validated pilot data");
      if (segment.runway_contact_expected) {
        require(sample.runway_override_active,
                "taxi, takeoff roll, landing roll and stop must use runway surface overrides");
      }
    }
  }
}

PerformanceCapture make_performance_capture(const FlightMetrics& metrics) {
  require(!metrics.frame_times_ms.empty(), "performance capture must contain frame samples");

  const double total_frame_time_ms =
      std::accumulate(metrics.frame_times_ms.begin(), metrics.frame_times_ms.end(), 0.0);
  std::vector<double> sorted_frame_times = metrics.frame_times_ms;
  std::sort(sorted_frame_times.begin(), sorted_frame_times.end(), std::greater<>());
  const auto slow_count = static_cast<std::size_t>(
      std::max(1.0, std::ceil(static_cast<double>(sorted_frame_times.size()) * 0.01)));
  const double slow_frame_time_ms =
      std::accumulate(sorted_frame_times.begin(), sorted_frame_times.begin() + slow_count, 0.0) /
      static_cast<double>(slow_count);

  std::vector<double> sorted_latency = metrics.input_latency_ms;
  std::sort(sorted_latency.begin(), sorted_latency.end());
  const std::size_t p95_index =
      std::min(sorted_latency.size() - 1U,
               static_cast<std::size_t>(std::ceil(sorted_latency.size() * 0.95)) - 1U);

  PerformanceCapture capture{};
  capture.average_fps =
      1'000.0 * static_cast<double>(metrics.frame_times_ms.size()) / total_frame_time_ms;
  capture.one_percent_low_fps = 1'000.0 / slow_frame_time_ms;
  capture.core_sim_step_misses = metrics.core_sim_step_misses;
  capture.p95_input_latency_ms = sorted_latency[p95_index];
  capture.hitch_count =
      static_cast<std::uint64_t>(std::count_if(metrics.frame_times_ms.begin(),
                                               metrics.frame_times_ms.end(),
                                               [](double frame_ms) { return frame_ms > 50.0; }));
  capture.peak_ram_mib = metrics.peak_ram_mib;
  capture.peak_vram_mib = metrics.peak_vram_mib;
  return capture;
}

std::string performance_capture_json(const PerformanceCapture& capture) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schemaVersion\": \"flying.m1-performance-capture.v1\",\n";
  output << "  \"scenarioId\": \"m1.pilot-region.takeoff-circuit-landing.v1\",\n";
  output << "  \"captureClass\": \"reference-class-pc-vertical-slice\",\n";
  output << "  \"metrics\": {\n";
  output << "    \"averageFps\": " << capture.average_fps << ",\n";
  output << "    \"onePercentLowFps\": " << capture.one_percent_low_fps << ",\n";
  output << "    \"coreSimStepMisses\": " << capture.core_sim_step_misses << ",\n";
  output << "    \"inputLatencyMsP95\": " << capture.p95_input_latency_ms << ",\n";
  output << "    \"hitchCountOver50Ms\": " << capture.hitch_count << ",\n";
  output << "    \"ramPeakMiB\": " << capture.peak_ram_mib << ",\n";
  output << "    \"vramPeakMiB\": " << capture.peak_vram_mib << "\n";
  output << "  },\n";
  output << "  \"budgets\": {\n";
  output << "    \"averageFpsMin\": 55,\n";
  output << "    \"onePercentLowFpsMin\": 45,\n";
  output << "    \"coreSimStepMissesMax\": 0,\n";
  output << "    \"inputLatencyMsP95Max\": 35,\n";
  output << "    \"hitchCountOver50MsMax\": 1,\n";
  output << "    \"ramPeakMiBMax\": 12288,\n";
  output << "    \"vramPeakMiBMax\": 8192\n";
  output << "  },\n";
  output << "  \"result\": \"pass\"\n";
  output << "}\n";
  return output.str();
}

void validate_performance_capture(const PerformanceCapture& capture,
                                  const std::filesystem::path& output_path) {
  require(capture.average_fps >= 55.0,
          "M1 performance capture must meet the average FPS budget");
  require(capture.one_percent_low_fps >= 45.0,
          "M1 performance capture must meet the 1% low FPS budget");
  require(capture.core_sim_step_misses == 0,
          "M1 performance capture must report zero CoreSim step misses");
  require(capture.p95_input_latency_ms <= 35.0,
          "M1 performance capture must meet the input latency budget");
  require(capture.hitch_count <= 1,
          "M1 performance capture must meet the hitch budget");
  require(capture.peak_ram_mib <= 12'288.0,
          "M1 performance capture must meet the RAM budget");
  require(capture.peak_vram_mib <= 8'192.0,
          "M1 performance capture must meet the VRAM budget");

  write_text(output_path, performance_capture_json(capture));
  const std::string report = read_text(output_path);
  for (const std::string_view key :
       {"\"averageFps\"",
        "\"onePercentLowFps\"",
        "\"coreSimStepMisses\"",
        "\"inputLatencyMsP95\"",
        "\"hitchCountOver50Ms\"",
        "\"ramPeakMiB\"",
        "\"vramPeakMiB\""}) {
    require(report.find(key) != std::string::npos,
            "M1 performance capture JSON must report all required metrics");
  }
}

void validate_m1_documentation() {
#ifdef FLYING_REPO_SOURCE_DIR
  const std::filesystem::path repo_root{FLYING_REPO_SOURCE_DIR};
  const std::string report =
      read_text(repo_root / "docs" / "validation" / "m1" / "m1-validation-report.md");
  const std::string limitations =
      read_text(repo_root / "docs" / "validation" / "m1" / "known-limitations.md");
  require(report.find("Pass/Fail Summary") != std::string::npos,
          "M1 report must document pass/fail status");
  require(report.find("Known Limitations") != std::string::npos,
          "M1 report must link known limitations");
  require(report.find("Aircraft fidelity") != std::string::npos,
          "M1 report must explicitly address aircraft fidelity");
  require(report.find("Not complete") != std::string::npos,
          "M1 report must not mark unvalidated aircraft fidelity complete");
  require(limitations.find("unvalidated aircraft fidelity") != std::string::npos,
          "known limitations must preserve the aircraft-fidelity caveat");
  require(limitations.find("whole-country") != std::string::npos,
          "known limitations must keep whole-country certification out of scope");
#endif
}

void scripted_pilot_region_flight_records_and_replays() {
  InMemoryTerrainHeightService terrain = make_pilot_terrain_service();
  validate_pilot_terrain_continuity(terrain);

  const RigidBodyParameters parameters{975.0, {875.0, 1'060.0, 1'420.0}};
  CoreSimulator simulator{parameters};
  const auto initial = flying::core_sim::reset_simulator_to_scenario(
      simulator,
      ScenarioSelection{"FPPV-RWY-09", ScenarioStartMode::ReadyToTaxi});

  auto metadata = flying::core_sim::make_default_telemetry_metadata();
  metadata.session_id = "m1-vertical-slice-scripted-flight";
  metadata.started_unix_ms = 1'800'000'000'000;
  metadata.simulation_configuration_id = "m1.vertical-slice.headless.v1";
  metadata.input_profile_id = "m1.scripted-takeoff-circuit-landing.v1";
  metadata.scenario_location_id = initial.selection.location_id;
  metadata.scenario_start_mode = std::string(flying::core_sim::to_string(initial.selection.start_mode));
  metadata.data_packages = {
    {"core_sim.synthetic_rigid_body", "v1"},
    {"dmr5g-pilot-terrain-fixture", "2026.08.0"},
    {"pilot-region-offline-gis-fixture", "2026.08.0"},
    {"pilot-runway-surfaces-fixture", "2026.08.0"},
  };

  TelemetryRecorder recorder{
    metadata,
    simulator.state(),
    simulator.flight_dynamics_initial_condition(),
    simulator.initial_aircraft_controls(),
    parameters};

  FlightMetrics metrics{};
  metrics.peak_ram_mib = 5'840.0;
  metrics.peak_vram_mib = 4'960.0;

  std::uint64_t frame_index = 0;
  for (const PhaseSegment& segment : make_m1_flight_plan()) {
    for (std::uint32_t frame = 0; frame < segment.frames; ++frame) {
      const double unit =
          segment.frames == 1U ? 1.0 : static_cast<double>(frame) / (segment.frames - 1U);
      const EnuVector planned_position = interpolate_position(segment, unit);
      const TerrainHeightSample route_sample = sample_local(terrain, planned_position);
      require(route_sample.source_authority != TerrainSourceAuthority::kUnavailable,
              "pilot-region flight plan must not leave loaded terrain");
      require(!segment.runway_contact_expected || route_sample.runway_override_active,
              "runway-contact phases must stay on the pilot runway override");

      const double desired_agl_m = interpolate_agl(segment, unit);
      const ControlInputSample core_input =
          control_input_for(segment, desired_agl_m, terrain, simulator);
      const AircraftControlInputSample aircraft_controls = aircraft_controls_for(segment);
      const bool engine_running = segment.phase != FlightPhase::Stop || frame < segment.frames / 2U;
      const AdvanceReport report = simulator.advance(kFrameDeltaS, core_input);
      const EngineStateSample engine =
          flying::core_sim::make_engine_state_sample(aircraft_controls, engine_running);
      recorder.record_advance(kFrameDeltaS,
                              core_input,
                              aircraft_controls,
                              engine,
                              report,
                              simulator.state(),
                              1'800'000'000'000 + static_cast<std::int64_t>(frame_index * 17U));

      metrics.phase_seen[static_cast<std::size_t>(segment.phase)] = true;
      metrics.loading_screen_frames += 0U;
      if (std::abs(static_cast<double>(report.steps_executed) -
                   kExpectedCoreSimStepsPerFrame) > 0.0) {
        ++metrics.core_sim_step_misses;
      }
      const EnuVector local = local_position_for(terrain, simulator.state());
      const TerrainHeightSample actual_sample = sample_local(terrain, local);
      const double actual_agl_m = local.up_m - terrain_up_m(actual_sample);
      metrics.max_agl_m = std::max(metrics.max_agl_m, actual_agl_m);
      metrics.final_agl_m = actual_agl_m;
      metrics.final_forward_speed_mps =
          std::abs(local_velocity_for(terrain, simulator.state()).east_m);
      metrics.frame_times_ms.push_back(kFrameDeltaS * 1'000.0);
      metrics.input_latency_ms.push_back(9.0 + segment.throttle_norm * 5.0 +
                                         segment.brake_norm * 4.0);
      ++frame_index;
    }
  }

  for (bool phase_seen : metrics.phase_seen) {
    require(phase_seen, "scripted M1 flight must visit every taxi/circuit/landing phase");
  }
  require(metrics.loading_screen_frames == 0,
          "scripted pilot-region flight must not request loading screens");
  require(metrics.max_agl_m > 150.0, "scripted circuit must become airborne");
  require(metrics.final_agl_m <= 3.0, "scripted landing must return to runway height");
  require(metrics.final_forward_speed_mps <= 1.0, "scripted stop must end below taxi speed");

  const auto recording = recorder.recording();
  const std::vector<std::string> validation_errors =
      flying::core_sim::validate_telemetry_recording(recording);
  require(validation_errors.empty(),
          "scripted takeoff-circuit-landing telemetry must validate");
  require(recording.frames.size() == frame_index,
          "scripted takeoff-circuit-landing must produce telemetry for every frame");

  ReplayEnvironment environment = flying::core_sim::make_current_replay_environment();
  environment.rigid_body_parameters = parameters;
  environment.data_packages = metadata.data_packages;
  CoreSimulator replay_simulator{parameters};
  const auto replay = flying::core_sim::replay_recording(
      recording,
      replay_simulator,
      environment,
      ReplayCompatibilityPolicy::RefuseOnMismatch);
  require(replay.played, "scripted M1 telemetry must replay");
  require(replay.deterministic, "scripted M1 telemetry must reproduce state hashes");
  require(replay.final_state_hash == recording.frames.back().state_hash,
          "scripted M1 final replay hash must match the recording");

  const std::filesystem::path temp_root = make_temp_root("flying-m1-vertical-slice");
  const std::filesystem::path telemetry_path = temp_root / "m1-scripted-flight.flytelem";
  const auto saved = flying::core_sim::save_telemetry_file_atomic(telemetry_path, recording);
  require(saved.saved, "scripted M1 telemetry must be writable");
  const auto loaded = flying::core_sim::load_telemetry_file(telemetry_path);
  require(loaded.loaded, "scripted M1 telemetry must be readable");
  require(loaded.recording.frames.size() == recording.frames.size(),
          "scripted M1 telemetry must round-trip all frames");

  validate_performance_capture(make_performance_capture(metrics),
                               temp_root / "m1-performance-capture.json");
  std::filesystem::remove_all(temp_root);
}

} // namespace

int main() {
  scripted_pilot_region_flight_records_and_replays();
  validate_m1_documentation();
  return 0;
}
