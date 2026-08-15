#include "CockpitSubsystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

namespace flying::presentation {
namespace {

[[nodiscard]] double clamp(double value, double minimum, double maximum) noexcept {
  return std::max(minimum, std::min(maximum, value));
}

[[nodiscard]] bool is_night(double local_time_seconds) noexcept {
  const double hours = std::fmod(std::max(0.0, local_time_seconds), 86400.0) / 3600.0;
  return hours < 6.0 || hours >= 20.0;
}

[[nodiscard]] CockpitControlDefinition control(
    std::string id,
    std::string label,
    CockpitControlKind kind,
    std::vector<CockpitWorkflowPhase> phases,
    double minimum = 0.0,
    double maximum = 1.0) {
  return {std::move(id), std::move(label), kind, minimum, maximum, std::move(phases)};
}

void add_instrument(std::vector<CockpitInstrumentDisplay>& instruments,
                    std::string id,
                    std::string label,
                    double value,
                    double secondary_value,
                    std::string unit,
                    std::string secondary_unit,
                    std::uint64_t sequence,
                    bool valid = true) {
  instruments.push_back({std::move(id),
                         std::move(label),
                         value,
                         secondary_value,
                         std::move(unit),
                         std::move(secondary_unit),
                         sequence,
                         true,
                         valid});
}

[[nodiscard]] double radians_to_degrees(double radians) noexcept {
  return radians * 180.0 / 3.14159265358979323846;
}

struct JsonValue {
  using Object = std::map<std::string, JsonValue>;
  using Array = std::vector<JsonValue>;
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;
};

class JsonParser {
public:
  explicit JsonParser(std::string text) : text_(std::move(text)) {}

  [[nodiscard]] JsonValue parse() {
    JsonValue parsed = parse_value();
    skip_ws();
    if (pos_ != text_.size()) {
      throw std::runtime_error("trailing content after JSON document");
    }
    return parsed;
  }

private:
  void skip_ws() {
    while (pos_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
      ++pos_;
    }
  }

  [[nodiscard]] char peek() {
    skip_ws();
    if (pos_ >= text_.size()) {
      throw std::runtime_error("unexpected end of JSON document");
    }
    return text_[pos_];
  }

  [[nodiscard]] bool consume(char expected) {
    skip_ws();
    if (pos_ < text_.size() && text_[pos_] == expected) {
      ++pos_;
      return true;
    }
    return false;
  }

  void expect(char expected) {
    if (!consume(expected)) {
      throw std::runtime_error("unexpected JSON token");
    }
  }

  [[nodiscard]] JsonValue parse_value() {
    const char next = peek();
    if (next == '{') {
      return JsonValue{parse_object()};
    }
    if (next == '[') {
      return JsonValue{parse_array()};
    }
    if (next == '"') {
      return JsonValue{parse_string()};
    }
    if (next == 't') {
      expect_literal("true");
      return JsonValue{true};
    }
    if (next == 'f') {
      expect_literal("false");
      return JsonValue{false};
    }
    if (next == 'n') {
      expect_literal("null");
      return JsonValue{nullptr};
    }
    return JsonValue{parse_number()};
  }

  [[nodiscard]] JsonValue::Object parse_object() {
    JsonValue::Object object;
    expect('{');
    if (consume('}')) {
      return object;
    }
    while (true) {
      std::string key = parse_string();
      expect(':');
      object.emplace(std::move(key), parse_value());
      if (consume('}')) {
        return object;
      }
      expect(',');
    }
  }

  [[nodiscard]] JsonValue::Array parse_array() {
    JsonValue::Array array;
    expect('[');
    if (consume(']')) {
      return array;
    }
    while (true) {
      array.push_back(parse_value());
      if (consume(']')) {
        return array;
      }
      expect(',');
    }
  }

  [[nodiscard]] std::string parse_string() {
    expect('"');
    std::string parsed;
    while (pos_ < text_.size()) {
      const char character = text_[pos_++];
      if (character == '"') {
        return parsed;
      }
      if (character == '\\') {
        if (pos_ >= text_.size()) {
          throw std::runtime_error("unterminated JSON escape");
        }
        const char escaped = text_[pos_++];
        if (escaped == '"' || escaped == '\\' || escaped == '/') {
          parsed.push_back(escaped);
        } else if (escaped == 'n') {
          parsed.push_back('\n');
        } else if (escaped == 'r') {
          parsed.push_back('\r');
        } else if (escaped == 't') {
          parsed.push_back('\t');
        } else {
          throw std::runtime_error("unsupported JSON escape");
        }
      } else {
        parsed.push_back(character);
      }
    }
    throw std::runtime_error("unterminated JSON string");
  }

  [[nodiscard]] double parse_number() {
    skip_ws();
    const std::size_t start = pos_;
    if (pos_ < text_.size() && text_[pos_] == '-') {
      ++pos_;
    }
    while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
      ++pos_;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
        ++pos_;
      }
    }
    if (pos_ > start && pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_])) != 0) {
        ++pos_;
      }
    }
    if (pos_ <= start) {
      throw std::runtime_error("expected JSON number");
    }
    return std::stod(text_.substr(start, pos_ - start));
  }

  void expect_literal(std::string_view literal) {
    for (const char character : literal) {
      if (pos_ >= text_.size() || text_[pos_] != character) {
        throw std::runtime_error("invalid JSON literal");
      }
      ++pos_;
    }
  }

  std::string text_;
  std::size_t pos_{};
};

[[nodiscard]] const JsonValue::Object& as_object(const JsonValue& value) {
  if (!std::holds_alternative<JsonValue::Object>(value.value)) {
    throw std::runtime_error("expected JSON object");
  }
  return std::get<JsonValue::Object>(value.value);
}

[[nodiscard]] const JsonValue::Array& as_array(const JsonValue& value) {
  if (!std::holds_alternative<JsonValue::Array>(value.value)) {
    throw std::runtime_error("expected JSON array");
  }
  return std::get<JsonValue::Array>(value.value);
}

[[nodiscard]] const std::string& as_string(const JsonValue& value) {
  if (!std::holds_alternative<std::string>(value.value)) {
    throw std::runtime_error("expected JSON string");
  }
  return std::get<std::string>(value.value);
}

[[nodiscard]] double as_number(const JsonValue& value) {
  if (!std::holds_alternative<double>(value.value)) {
    throw std::runtime_error("expected JSON number");
  }
  return std::get<double>(value.value);
}

[[nodiscard]] bool as_bool(const JsonValue& value) {
  if (!std::holds_alternative<bool>(value.value)) {
    throw std::runtime_error("expected JSON boolean");
  }
  return std::get<bool>(value.value);
}

[[nodiscard]] const JsonValue& member(const JsonValue::Object& object, const std::string& key) {
  const auto found = object.find(key);
  if (found == object.end()) {
    throw std::runtime_error("missing JSON member: " + key);
  }
  return found->second;
}

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("file is unavailable: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

std::vector<CockpitControlDefinition> make_default_cockpit_controls() {
  using Phase = CockpitWorkflowPhase;
  using Kind = CockpitControlKind;

  return {
      control("battery_master", "Battery master", Kind::Toggle, {Phase::EngineStart, Phase::Shutdown}),
      control("alternator", "Alternator", Kind::Toggle, {Phase::EngineStart, Phase::Flight, Phase::Shutdown}),
      control("avionics_master", "Avionics master", Kind::Toggle, {Phase::EngineStart, Phase::Flight, Phase::Shutdown}),
      control("magnetos", "Magnetos", Kind::Selector, {Phase::EngineStart, Phase::Flight, Phase::Shutdown}, 0.0, 4.0),
      control("starter", "Starter", Kind::Momentary, {Phase::EngineStart}),
      control("mixture", "Mixture", Kind::Axis, {Phase::EngineStart, Phase::Taxi, Phase::Takeoff, Phase::Flight, Phase::Landing, Phase::Shutdown}),
      control("throttle", "Throttle", Kind::Axis, {Phase::EngineStart, Phase::Taxi, Phase::Takeoff, Phase::Flight, Phase::Landing, Phase::Shutdown}),
      control("carb_heat", "Carb heat", Kind::Axis, {Phase::EngineStart, Phase::Flight, Phase::Landing}),
      control("fuel_selector", "Fuel selector", Kind::Selector, {Phase::EngineStart, Phase::Taxi, Phase::Takeoff, Phase::Flight, Phase::Landing, Phase::Shutdown}, 0.0, 3.0),
      control("electric_fuel_pump", "Electric fuel pump", Kind::Toggle, {Phase::EngineStart, Phase::Takeoff, Phase::Landing}),
      control("parking_brake", "Parking brake", Kind::Toggle, {Phase::EngineStart, Phase::Taxi, Phase::Shutdown}),
      control("toe_brakes", "Toe brakes", Kind::Axis, {Phase::Taxi, Phase::Landing, Phase::Shutdown}),
      control("rudder_pedals", "Rudder pedals", Kind::Axis, {Phase::Taxi, Phase::Takeoff, Phase::Flight, Phase::Landing}, -1.0, 1.0),
      control("yoke_pitch", "Yoke pitch", Kind::Axis, {Phase::Takeoff, Phase::Flight, Phase::Landing}, -1.0, 1.0),
      control("yoke_roll", "Yoke roll", Kind::Axis, {Phase::Takeoff, Phase::Flight, Phase::Landing}, -1.0, 1.0),
      control("elevator_trim", "Elevator trim", Kind::Axis, {Phase::Takeoff, Phase::Flight, Phase::Landing}, -1.0, 1.0),
      control("flaps", "Flaps", Kind::Selector, {Phase::Takeoff, Phase::Flight, Phase::Landing}, 0.0, 3.0),
      control("pitot_heat", "Pitot heat", Kind::Toggle, {Phase::Flight, Phase::Landing}),
      control("landing_light", "Landing light", Kind::Toggle, {Phase::Taxi, Phase::Takeoff, Phase::Landing}),
      control("taxi_light", "Taxi light", Kind::Toggle, {Phase::Taxi}),
      control("nav_lights", "Navigation lights", Kind::Toggle, {Phase::Taxi, Phase::Takeoff, Phase::Flight, Phase::Landing, Phase::Shutdown}),
      control("beacon_light", "Beacon light", Kind::Toggle, {Phase::EngineStart, Phase::Taxi, Phase::Takeoff, Phase::Flight, Phase::Landing, Phase::Shutdown}),
      control("instrument_dimmer", "Instrument dimmer", Kind::Axis, {Phase::EngineStart, Phase::Taxi, Phase::Takeoff, Phase::Flight, Phase::Landing, Phase::Shutdown}),
      control("cockpit_flood_light", "Cockpit flood light", Kind::Axis, {Phase::EngineStart, Phase::Taxi, Phase::Takeoff, Phase::Flight, Phase::Landing, Phase::Shutdown}),
  };
}

bool cockpit_phase_requires_control(const CockpitControlDefinition& control,
                                    CockpitWorkflowPhase phase) {
  return std::find(control.required_for.begin(), control.required_for.end(), phase) !=
         control.required_for.end();
}

CockpitScreenshotComparisonResult compare_cockpit_screenshot_state(
    const CockpitScreenshotComparisonBaseline& baseline,
    const CockpitRasterImage& expected,
    const CockpitRasterImage& captured) {
  if (expected.width != captured.width ||
      expected.height != captured.height ||
      expected.rgba.size() != captured.rgba.size() ||
      expected.rgba.empty()) {
    return {baseline.id, 100.0, false};
  }

  double total_channel_error = 0.0;
  for (std::size_t index = 0; index < expected.rgba.size(); ++index) {
    total_channel_error +=
        std::abs(static_cast<int>(expected.rgba[index]) - static_cast<int>(captured.rgba[index]));
  }
  const double difference_percent =
      total_channel_error / (static_cast<double>(expected.rgba.size()) * 255.0) * 100.0;
  return {baseline.id, difference_percent, difference_percent <= baseline.maximum_difference_percent};
}

CockpitLayoutLoadResult load_cockpit_layout(const std::filesystem::path& path) {
  CockpitLayoutLoadResult result;
  try {
    JsonParser parser(read_file(path));
    const JsonValue document = parser.parse();
    const auto& root = as_object(document);
    result.layout.schema_version = as_string(member(root, "schemaVersion"));
    result.layout.aircraft_id = as_string(member(root, "aircraftId"));

    const auto& materials = as_object(member(root, "materials"));
    result.layout.minimum_pixels_per_capital_height =
        static_cast<int>(as_number(member(as_object(member(materials, "placards")),
                                          "minimumPixelsPerCapitalHeight")));

    const auto& lighting = as_object(member(root, "lighting"));
    result.layout.minimum_night_readability_score =
        as_number(member(lighting, "minimumNightReadabilityScore"));
    result.layout.casts_panel_shadows = as_bool(member(lighting, "castsPanelShadows"));

    for (const auto& instrument_value : as_array(member(root, "instruments"))) {
      const auto& instrument = as_object(instrument_value);
      CockpitInstrumentBinding binding;
      binding.id = as_string(member(instrument, "id"));
      binding.source = as_string(member(instrument, "source"));
      binding.animation = as_string(member(instrument, "animation"));
      binding.units = as_string(member(instrument, "units"));
      if (const auto channels = instrument.find("channels"); channels != instrument.end()) {
        for (const auto& channel : as_array(channels->second)) {
          binding.channels.push_back(as_string(channel));
        }
      }
      result.layout.instruments.push_back(std::move(binding));
    }

    for (const auto& baseline_value : as_array(member(root, "screenshotBaselines"))) {
      const auto& baseline = as_object(baseline_value);
      result.layout.screenshot_baselines.push_back({
          as_string(member(baseline, "id")),
          as_string(member(baseline, "view")),
          path.parent_path() / as_string(member(baseline, "image")),
          as_number(member(baseline, "maxDifferencePercent"))});
    }

    const auto repo_root = path.parent_path().parent_path().parent_path();
    const auto& capture = as_object(member(root, "screenshotCapture"));
    result.layout.screenshot_capture_provenance_path =
        repo_root / as_string(member(capture, "provenance"));
    result.layout.screenshot_capture_command = as_string(member(capture, "renderCommand"));

    for (const auto& view_state : as_array(member(lighting, "nightViewStates"))) {
      result.layout.night_view_states.push_back(as_string(view_state));
    }

    const auto& screenshot_requirements =
        as_object(member(root, "screenshotRequirements"));
    result.layout.minimum_screenshot_width =
        static_cast<int>(as_number(member(screenshot_requirements, "minimumWidth")));
    result.layout.minimum_screenshot_height =
        static_cast<int>(as_number(member(screenshot_requirements, "minimumHeight")));

    for (const auto& control_value : as_array(member(root, "controls"))) {
      const auto& control = as_object(control_value);
      CockpitControlBinding binding;
      binding.id = as_string(member(control, "id"));
      binding.binding = as_string(member(control, "binding"));
      binding.kind = as_string(member(control, "kind"));
      for (const auto& phase : as_array(member(control, "requiredFor"))) {
        binding.required_for.push_back(as_string(phase));
      }
      result.layout.controls.push_back(std::move(binding));
    }

    for (const auto& hook_value : as_array(member(root, "audioHooks"))) {
      const auto& hook = as_object(hook_value);
      CockpitAudioHookBinding binding;
      binding.id = as_string(member(hook, "id"));
      binding.source = as_string(member(hook, "source"));
      for (const auto& parameter : as_array(member(hook, "parameters"))) {
        binding.parameters.push_back(as_string(parameter));
      }
      result.layout.audio_hooks.push_back(std::move(binding));
    }

    result.loaded = result.layout.schema_version == "flying.cockpit.layout.v1";
    if (!result.loaded) {
      result.errors.push_back("unsupported cockpit layout schema: " + result.layout.schema_version);
    }
  } catch (const std::exception& error) {
    result.loaded = false;
    result.errors.push_back(error.what());
  }
  return result;
}

CockpitRasterImage load_cockpit_ppm_screenshot(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("screenshot baseline is unavailable: " + path.string());
  }

  std::string magic;
  int maximum = 0;
  CockpitRasterImage image;
  input >> magic >> image.width >> image.height >> maximum;
  if (magic != "P3" && magic != "P6") {
    throw std::runtime_error("unsupported screenshot baseline format: " + path.string());
  }
  if (image.width <= 0 || image.height <= 0 || maximum != 255) {
    throw std::runtime_error("unsupported screenshot baseline format: " + path.string());
  }

  image.rgba.reserve(static_cast<std::size_t>(image.width * image.height * 4));
  if (magic == "P3") {
    for (int index = 0; index < image.width * image.height; ++index) {
      int r = 0;
      int g = 0;
      int b = 0;
      input >> r >> g >> b;
      if (!input || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        throw std::runtime_error("invalid screenshot baseline pixel data: " + path.string());
      }
      image.rgba.push_back(static_cast<std::uint8_t>(r));
      image.rgba.push_back(static_cast<std::uint8_t>(g));
      image.rgba.push_back(static_cast<std::uint8_t>(b));
      image.rgba.push_back(255);
    }
  } else {
    input.get();
    std::vector<char> rgb(static_cast<std::size_t>(image.width * image.height * 3));
    input.read(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    if (input.gcount() != static_cast<std::streamsize>(rgb.size())) {
      throw std::runtime_error("invalid screenshot baseline pixel data: " + path.string());
    }
    for (std::size_t index = 0; index < rgb.size(); index += 3) {
      image.rgba.push_back(static_cast<std::uint8_t>(rgb[index]));
      image.rgba.push_back(static_cast<std::uint8_t>(rgb[index + 1]));
      image.rgba.push_back(static_cast<std::uint8_t>(rgb[index + 2]));
      image.rgba.push_back(255);
    }
  }
  return image;
}

CockpitSubsystem::CockpitSubsystem() : controls_(make_default_cockpit_controls()) {
  frame_.controls.reserve(controls_.size());
  for (std::size_t index = 0; index < controls_.size(); ++index) {
    control_index_by_id_[controls_[index].id] = index;
    frame_.controls.push_back({controls_[index].id, controls_[index].minimum, true});
  }
}

const std::vector<CockpitControlDefinition>& CockpitSubsystem::controls() const noexcept {
  return controls_;
}

const CockpitPresentationFrame& CockpitSubsystem::frame() const noexcept {
  return frame_;
}

const CockpitAircraftCommandState& CockpitSubsystem::command_state() const noexcept {
  return frame_.commands;
}

bool CockpitSubsystem::set_control_value(const std::string& id, double value) {
  const auto found = control_index_by_id_.find(id);
  if (found == control_index_by_id_.end()) {
    return false;
  }
  const auto index = found->second;
  const double clamped = clamp(value, controls_[index].minimum, controls_[index].maximum);
  frame_.controls[index].value = clamped;
  dispatch_control_to_aircraft(controls_[index], clamped);
  return true;
}

void CockpitSubsystem::dispatch_control_to_aircraft(const CockpitControlDefinition& control,
                                                    double value) {
  auto& aircraft = frame_.commands.aircraft_controls;
  auto& switches = frame_.commands.system_switches;
  const bool enabled = value >= 0.5;

  if (control.id == "battery_master") {
    switches.battery_master_on = enabled;
  } else if (control.id == "alternator") {
    switches.alternator_on = enabled;
  } else if (control.id == "avionics_master") {
    switches.avionics_master_on = enabled;
  } else if (control.id == "pitot_heat") {
    switches.pitot_heat_on = enabled;
  } else if (control.id == "electric_fuel_pump") {
    switches.electric_fuel_pump_on = enabled;
  } else if (control.id == "magnetos") {
    aircraft.magnetos_on = value >= 1.0;
    aircraft.engine_run_switch = value >= 1.0;
    aircraft.engine_starter_engaged = value >= 3.5;
  } else if (control.id == "starter") {
    aircraft.engine_starter_engaged = enabled;
  } else if (control.id == "mixture") {
    aircraft.mixture_norm = value;
    aircraft.engine_run_switch = value > 0.05 && aircraft.magnetos_on;
  } else if (control.id == "throttle") {
    aircraft.throttle_norm = value;
  } else if (control.id == "fuel_selector") {
    if (value < 0.5) {
      frame_.commands.fuel_selector = core_sim::FuelTankSelector::off;
      aircraft.engine_run_switch = false;
    } else if (value < 1.5) {
      frame_.commands.fuel_selector = core_sim::FuelTankSelector::left;
    } else if (value < 2.5) {
      frame_.commands.fuel_selector = core_sim::FuelTankSelector::right;
    } else {
      frame_.commands.fuel_selector = core_sim::FuelTankSelector::both;
    }
  } else if (control.id == "parking_brake") {
    frame_.commands.parking_brake_set = enabled;
    aircraft.brake_left_norm = enabled ? 1.0 : 0.0;
    aircraft.brake_right_norm = enabled ? 1.0 : 0.0;
  } else if (control.id == "toe_brakes") {
    aircraft.brake_left_norm = value;
    aircraft.brake_right_norm = value;
  } else if (control.id == "rudder_pedals") {
    aircraft.rudder_norm = value;
  } else if (control.id == "yoke_pitch") {
    aircraft.elevator_norm = value;
  } else if (control.id == "yoke_roll") {
    aircraft.aileron_norm = value;
  } else if (control.id == "elevator_trim") {
    aircraft.elevator_trim_norm = value;
  } else if (control.id == "flaps") {
    aircraft.flaps_norm = value / control.maximum;
  } else if (control.id == "landing_light") {
    frame_.commands.landing_light_on = enabled;
  } else if (control.id == "taxi_light") {
    frame_.commands.taxi_light_on = enabled;
  } else if (control.id == "nav_lights") {
    frame_.commands.navigation_lights_on = enabled;
  } else if (control.id == "beacon_light") {
    frame_.commands.beacon_light_on = enabled;
  }
}

bool CockpitSubsystem::control_is_operable_for(CockpitWorkflowPhase phase) const {
  bool phase_has_required_control = false;
  for (const auto& definition : controls_) {
    if (!cockpit_phase_requires_control(definition, phase)) {
      continue;
    }
    phase_has_required_control = true;
    const auto found = control_index_by_id_.find(definition.id);
    if (found == control_index_by_id_.end() || !frame_.controls[found->second].operable) {
      return false;
    }
  }
  return phase_has_required_control;
}

bool CockpitSubsystem::has_required_controls_for_all_workflow_phases() const {
  return control_is_operable_for(CockpitWorkflowPhase::EngineStart) &&
         control_is_operable_for(CockpitWorkflowPhase::Taxi) &&
         control_is_operable_for(CockpitWorkflowPhase::Takeoff) &&
         control_is_operable_for(CockpitWorkflowPhase::Flight) &&
         control_is_operable_for(CockpitWorkflowPhase::Landing) &&
         control_is_operable_for(CockpitWorkflowPhase::Shutdown);
}

CockpitPresentationFrame CockpitSubsystem::update_from_instruments(
    const core_sim::InstrumentData& instruments,
    double local_time_seconds) {
  frame_.instruments.clear();
  add_instrument(frame_.instruments,
                 "airspeed_indicator",
                 "Airspeed",
                 instruments.indicated_airspeed_mps,
                 0.0,
                 "m/s",
                 "",
                 instruments.sequence,
                 !instruments.pitot_static.pitot_blocked);
  add_instrument(frame_.instruments,
                 "altimeter",
                 "Altimeter",
                 instruments.indicated_altitude_m,
                 0.0,
                 "m",
                 "",
                 instruments.sequence,
                 !instruments.pitot_static.static_blocked);
  add_instrument(frame_.instruments,
                 "vertical_speed_indicator",
                 "Vertical speed",
                 instruments.vertical_speed_mps,
                 0.0,
                 "m/s",
                 "",
                 instruments.sequence,
                 !instruments.pitot_static.static_blocked);
  add_instrument(frame_.instruments,
                 "magnetic_compass",
                 "Compass",
                 radians_to_degrees(instruments.magnetic_heading_rad),
                 0.0,
                 "deg",
                 "",
                 instruments.sequence);
  add_instrument(frame_.instruments,
                 "attitude_indicator",
                 "Attitude",
                 radians_to_degrees(instruments.attitude_roll_rad),
                 radians_to_degrees(instruments.attitude_pitch_rad),
                 "deg",
                 "deg",
                 instruments.sequence,
                 instruments.vacuum.suction_inhg > 3.5);
  add_instrument(frame_.instruments,
                 "directional_gyro",
                 "Directional gyro",
                 radians_to_degrees(instruments.gyro_heading_rad),
                 0.0,
                 "deg",
                 "",
                 instruments.sequence,
                 instruments.vacuum.suction_inhg > 3.5);
  add_instrument(frame_.instruments,
                 "tachometer",
                 "Tachometer",
                 instruments.engine.rpm,
                 0.0,
                 "rpm",
                 "",
                 instruments.sequence,
                 instruments.engine.valid);
  add_instrument(frame_.instruments,
                 "fuel_flow",
                 "Fuel flow",
                 instruments.engine.fuel_flow_kgps,
                 0.0,
                 "kg/s",
                 "",
                 instruments.sequence,
                 instruments.engine.valid);
  add_instrument(frame_.instruments,
                 "fuel_quantity_left",
                 "Left fuel",
                 instruments.fuel.tanks.left_quantity_kg,
                 0.0,
                 "kg",
                 "",
                 instruments.sequence);
  add_instrument(frame_.instruments,
                 "fuel_quantity_right",
                 "Right fuel",
                 instruments.fuel.tanks.right_quantity_kg,
                 0.0,
                 "kg",
                 "",
                 instruments.sequence);
  add_instrument(frame_.instruments,
                 "bus_voltage",
                 "Bus voltage",
                 instruments.electrical.bus_voltage_v,
                 0.0,
                 "V",
                 "",
                 instruments.sequence,
                 instruments.electrical.bus_voltage_v > 10.0);
  add_instrument(frame_.instruments,
                 "gps_ground_speed",
                 "GPS ground speed",
                 instruments.gps.ground_speed_mps,
                 0.0,
                 "m/s",
                 "",
                 instruments.sequence,
                 instruments.gps.valid);

  const bool night = is_night(local_time_seconds);
  const auto dimmer = control_index_by_id_.find("instrument_dimmer");
  const auto flood = control_index_by_id_.find("cockpit_flood_light");
  const double dimmer_value =
      dimmer == control_index_by_id_.end() ? 0.0 : frame_.controls[dimmer->second].value;
  const double flood_value =
      flood == control_index_by_id_.end() ? 0.0 : frame_.controls[flood->second].value;
  frame_.lighting = {dimmer_value,
                     flood_value,
                     night ? clamp(0.35 + 0.45 * dimmer_value + 0.20 * flood_value, 0.0, 1.0)
                           : 1.0,
                     night};

  const auto throttle = control_index_by_id_.find("throttle");
  frame_.engine_sound.rpm = instruments.engine.rpm;
  frame_.engine_sound.throttle_norm =
      throttle == control_index_by_id_.end() ? 0.0 : frame_.controls[throttle->second].value;
  frame_.engine_sound.fuel_flow_kgps = instruments.engine.fuel_flow_kgps;
  frame_.engine_sound.active = instruments.engine.rpm > 250.0 && instruments.engine.valid;

  return frame_;
}

} // namespace flying::presentation
