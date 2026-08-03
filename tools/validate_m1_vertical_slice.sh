#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake 3.25+ is required to run M1 vertical-slice validation." >&2
  exit 127
fi

cd "$repo_root"

cmake --preset test
cmake --build --preset test --target \
  flying_core_sim_kernel_tests \
  flying_core_sim_flight_dynamics_api_tests \
  flying_core_sim_input_mapping_tests \
  flying_core_sim_scenario_start_tests \
  flying_core_sim_terrain_contact_tests \
  flying_core_sim_telemetry_replay_tests \
  flying_geo_terrain_geodesy_units_tests \
  flying_geo_terrain_terrain_service_tests \
  flying_data_pipeline_dmr5g_terrain_tests \
  flying_data_pipeline_runway_importer_tests \
  flying_data_pipeline_pilot_region_package_tests \
  flying_m1_vertical_slice_tests
ctest --preset m1-vertical-slice --output-on-failure
