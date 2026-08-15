#include "CockpitSubsystem.h"

#include <cmath>
#include <filesystem>
#include <set>
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

const flying::presentation::CockpitInstrumentDisplay& find_instrument(
    const flying::presentation::CockpitPresentationFrame& frame,
    const std::string& id) {
  for (const auto& instrument : frame.instruments) {
    if (instrument.id == id) {
      return instrument;
    }
  }
  throw std::runtime_error("missing instrument: " + id);
}

const flying::presentation::CockpitControlState& find_control(
    const flying::presentation::CockpitPresentationFrame& frame,
    const std::string& id) {
  for (const auto& control : frame.controls) {
    if (control.id == id) {
      return control;
    }
  }
  throw std::runtime_error("missing control: " + id);
}

const flying::presentation::CockpitScreenshotComparisonBaseline& find_baseline(
    const flying::presentation::CockpitLayout& layout,
    const std::string& id) {
  for (const auto& baseline : layout.screenshot_baselines) {
    if (baseline.id == id) {
      return baseline;
    }
  }
  throw std::runtime_error("missing screenshot baseline: " + id);
}

void require_production_sized(
    const flying::presentation::CockpitRasterImage& image,
    const flying::presentation::CockpitLayout& layout,
    const char* message) {
  require(image.width >= layout.minimum_screenshot_width &&
              image.height >= layout.minimum_screenshot_height,
          message);
  require(image.rgba.size() ==
              static_cast<std::size_t>(image.width * image.height * 4),
          "screenshot image data must match declared dimensions");
}

flying::core_sim::InstrumentData sample_sensor_data() {
  flying::core_sim::InstrumentData data{};
  data.indicated_airspeed_mps = 42.0;
  data.indicated_altitude_m = 512.5;
  data.vertical_speed_mps = 1.75;
  data.magnetic_heading_rad = 1.5707963267948966;
  data.attitude_roll_rad = 0.10;
  data.attitude_pitch_rad = -0.05;
  data.gyro_heading_rad = 1.20;
  data.gps.ground_speed_mps = 44.0;
  data.gps.valid = true;
  data.engine.rpm = 2350.0;
  data.engine.fuel_flow_kgps = 0.010;
  data.engine.valid = true;
  data.fuel.tanks.left_quantity_kg = 37.0;
  data.fuel.tanks.right_quantity_kg = 35.0;
  data.electrical.bus_voltage_v = 14.1;
  data.vacuum.suction_inhg = 5.0;
  data.sequence = 77;
  return data;
}

void all_required_controls_are_operable_for_workflow() {
  flying::presentation::CockpitSubsystem cockpit;

  require(cockpit.controls().size() >= 20,
          "ordinary flight cockpit must expose a complete control set");
  require(cockpit.has_required_controls_for_all_workflow_phases(),
          "start, taxi, takeoff, flight, landing, and shutdown controls must be operable");

  require(cockpit.set_control_value("throttle", 1.5),
          "throttle binding must be accepted");
  require_near(find_control(cockpit.frame(), "throttle").value,
               1.0,
               1.0e-12,
               "axis controls must clamp to their physical range");
  require(cockpit.set_control_value("yoke_roll", -0.25),
          "primary flight control binding must be accepted");
  require_near(cockpit.command_state().aircraft_controls.throttle_norm,
               1.0,
               1.0e-12,
               "throttle control must dispatch to aircraft controls");
  require_near(cockpit.command_state().aircraft_controls.aileron_norm,
               -0.25,
               1.0e-12,
               "yoke roll control must dispatch to aircraft controls");
  require(cockpit.set_control_value("battery_master", 1.0),
          "battery master binding must be accepted");
  require(cockpit.command_state().system_switches.battery_master_on,
          "battery master must dispatch to aircraft system switches");
  require(cockpit.set_control_value("magnetos", 4.0), "magneto start binding must be accepted");
  require(cockpit.command_state().aircraft_controls.engine_starter_engaged,
          "magneto start position must dispatch starter engagement");
  require(cockpit.command_state().aircraft_controls.magnetos_on,
          "magneto selector must dispatch ignition state");
  require(cockpit.set_control_value("starter", 1.0), "starter press must be accepted");
  require(cockpit.command_state().aircraft_controls.engine_starter_engaged,
          "starter press must engage the starter command");
  require(cockpit.set_control_value("starter", 0.0), "starter release must be accepted");
  require(!cockpit.command_state().aircraft_controls.engine_starter_engaged,
          "starter release must clear the momentary starter command");
  require(cockpit.set_control_value("fuel_selector", 0.0),
          "fuel selector binding must be accepted");
  require(cockpit.command_state().fuel_selector == flying::core_sim::FuelTankSelector::off,
          "fuel selector must dispatch tank selection");
  require(cockpit.set_control_value("parking_brake", 1.0),
          "parking brake binding must be accepted");
  require(cockpit.command_state().parking_brake_set,
          "parking brake must dispatch brake hold state");
  require(!cockpit.set_control_value("unsupported_control", 1.0),
          "unsupported cockpit bindings must fail clearly");
}

void instruments_are_driven_by_sensor_outputs() {
  flying::presentation::CockpitSubsystem cockpit;
  require(cockpit.set_control_value("throttle", 0.65), "throttle must be controllable");

  const auto frame = cockpit.update_from_instruments(sample_sensor_data(), 13.0 * 3600.0);
  require(frame.instruments.size() >= 12,
          "cockpit must animate the expected flight, engine, fuel, electrical, and GPS instruments");

  const auto& asi = find_instrument(frame, "airspeed_indicator");
  require(asi.sensor_driven, "airspeed must be explicitly sensor-driven");
  require_near(asi.value, 42.0, 1.0e-12, "airspeed must come from pitot-static output");
  require(asi.sensor_sequence == 77, "instrument values must carry the sensor sequence");

  const auto& tach = find_instrument(frame, "tachometer");
  require(tach.sensor_driven, "tachometer must be explicitly sensor-driven");
  require_near(tach.value, 2350.0, 1.0e-12, "tachometer must come from engine sensor output");

  const auto& attitude = find_instrument(frame, "attitude_indicator");
  require(attitude.sensor_driven, "attitude indicator must be explicitly sensor-driven");
  require_near(attitude.value,
               0.10 * 180.0 / 3.14159265358979323846,
               1.0e-12,
               "attitude indicator primary value must come from gyro roll output");
  require_near(attitude.secondary_value,
               -0.05 * 180.0 / 3.14159265358979323846,
               1.0e-12,
               "attitude indicator secondary value must come from gyro pitch output");

  require(frame.engine_sound.active, "engine sound hook must activate from engine sensor output");
  require_near(frame.engine_sound.rpm, 2350.0, 1.0e-12, "engine sound rpm must follow sensors");
  require_near(frame.engine_sound.throttle_norm,
               0.65,
               1.0e-12,
               "engine sound throttle parameter must follow cockpit control");
}

void night_lighting_supports_readable_labels_and_screenshot_states() {
  flying::presentation::CockpitSubsystem cockpit;
  require(cockpit.set_control_value("instrument_dimmer", 0.8),
          "instrument dimmer must be controllable");
  require(cockpit.set_control_value("cockpit_flood_light", 0.6),
          "cockpit flood light must be controllable");

  const auto night_frame = cockpit.update_from_instruments(sample_sensor_data(), 22.0 * 3600.0);
  require(night_frame.lighting.night_mode, "night view must enter night lighting mode");
  require(night_frame.lighting.label_readability_score >= 0.72,
          "night labels must meet the approved readability threshold");

  const auto loaded_layout =
      flying::presentation::load_cockpit_layout(repo_path("Content/Cockpit/CockpitLayout.json"));
  require(loaded_layout.loaded, "production cockpit layout loader must load layout metadata");
  const auto& layout = loaded_layout.layout;
  require(layout.schema_version == "flying.cockpit.layout.v1",
          "cockpit layout manifest must declare its schema");
  require(layout.casts_panel_shadows, "cockpit layout must require panel shadows");
  require(layout.minimum_pixels_per_capital_height >= 12,
          "cockpit labels must declare a readable texture threshold");
  require(layout.minimum_screenshot_width >= 640 && layout.minimum_screenshot_height >= 360,
          "screenshot comparison must require production-sized images");
  require(!layout.screenshot_capture_provenance_path.empty(),
          "screenshot capture provenance path must be declared");
  require(!layout.screenshot_capture_command.empty(),
          "screenshot capture render command must be declared");
  require(layout.controls.size() == cockpit.controls().size(),
          "production layout loader must validate cockpit control metadata");
  require(!layout.audio_hooks.empty(),
          "production layout loader must validate cockpit audio hook metadata");

  std::set<std::string> subsystem_control_ids;
  for (const auto& control : cockpit.controls()) {
    subsystem_control_ids.insert(control.id);
  }
  for (const auto& binding : layout.controls) {
    require(subsystem_control_ids.erase(binding.id) == 1,
            "each layout control binding must map to a known cockpit subsystem control id");
    require(!binding.binding.empty(), "layout control binding must declare a command path");
    require(!binding.required_for.empty(), "layout control binding must declare workflow use");
  }
  require(subsystem_control_ids.empty(),
          "every cockpit subsystem control must have a layout binding");

  for (const auto& instrument : layout.instruments) {
    const auto& display = find_instrument(night_frame, instrument.id);
    if (instrument.id == "attitude_indicator") {
      require(instrument.animation == "horizon", "attitude indicator must use horizon animation");
      require(instrument.channels.size() == 2 &&
                  instrument.channels[0] == "roll" &&
                  instrument.channels[1] == "pitch",
              "attitude horizon binding must declare roll and pitch channels");
      require(display.secondary_unit == "deg",
              "attitude horizon display must expose a pitch unit for the second channel");
    }
  }

  require(layout.screenshot_baselines.size() >= 3,
          "required screenshot comparison baselines must be declared");

  flying::presentation::CockpitSubsystem day_cockpit;
  const auto day_frame = day_cockpit.update_from_instruments(sample_sensor_data(), 12.0 * 3600.0);
  require(!day_frame.lighting.night_mode, "day view must remain outside night lighting mode");
  const auto& day_baseline = find_baseline(layout, "day_readability");
  const auto expected_day = flying::presentation::load_cockpit_ppm_screenshot(day_baseline.image_path);
  require_production_sized(expected_day, layout, "day screenshot baseline must be production-sized");

  require(layout.night_view_states.size() == 5,
          "all required cockpit night view states must be declared");
  for (const auto& view_state : layout.night_view_states) {
    const auto& baseline = find_baseline(layout, view_state);
    const auto expected = flying::presentation::load_cockpit_ppm_screenshot(baseline.image_path);
    require_production_sized(expected, layout, "night screenshot baseline must be production-sized");
  }
}

} // namespace

int main() {
  all_required_controls_are_operable_for_workflow();
  instruments_are_driven_by_sensor_outputs();
  night_lighting_supports_readable_labels_and_screenshot_states();
  return 0;
}
