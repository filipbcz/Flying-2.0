#pragma once

#include "flying/core_sim/input.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace flying::presentation {

using InputProfileSettings = core_sim::InputSettings;
using InputProfileLoadResult = core_sim::InputSettingsLoadResult;
using InputProfileWriteResult = core_sim::InputSettingsWriteResult;

[[nodiscard]] InputProfileSettings make_default_input_profile_settings();
[[nodiscard]] std::vector<std::string> validate_input_profile_settings(
    const InputProfileSettings& settings);
[[nodiscard]] core_sim::MappedInputState map_input_profile_frame(
    const InputProfileSettings& settings,
    const core_sim::RawInputFrame& frame);
[[nodiscard]] double apply_input_profile_axis_response(
    double raw_value,
    const core_sim::AxisCalibration& calibration) noexcept;
[[nodiscard]] std::string serialize_input_profiles_schema_json(
    const InputProfileSettings& settings);
[[nodiscard]] InputProfileLoadResult load_input_profiles(
    const std::filesystem::path& path);
[[nodiscard]] InputProfileWriteResult save_input_profiles_atomic(
    const std::filesystem::path& path,
    const InputProfileSettings& settings);

} // namespace flying::presentation
