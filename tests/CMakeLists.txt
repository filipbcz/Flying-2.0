add_executable(flying_skeleton_smoke
  smoke/skeleton_smoke_test.cpp
)

target_link_libraries(flying_skeleton_smoke
  PRIVATE
    Flying::CoreSim
    Flying::GeoTerrain
    Flying::DataPipeline
    Flying::CompilerOptions
)

add_test(NAME flying.skeleton_smoke COMMAND flying_skeleton_smoke)

add_executable(flying_data_pipeline_manifest_tests
  data_pipeline/manifest_test.cpp
)

target_link_libraries(flying_data_pipeline_manifest_tests
  PRIVATE
    Flying::DataPipeline
    Flying::CompilerOptions
)

add_test(NAME flying.data_pipeline.manifest COMMAND flying_data_pipeline_manifest_tests)

add_test(NAME flying.data_pipeline.cli_help COMMAND flying_data_pipeline_cli --help)

add_executable(flying_data_pipeline_dmr5g_terrain_tests
  data_pipeline/dmr5g_terrain_test.cpp
)

target_link_libraries(flying_data_pipeline_dmr5g_terrain_tests
  PRIVATE
    Flying::DataPipeline
    Flying::CompilerOptions
)

add_test(NAME flying.data_pipeline.dmr5g_terrain COMMAND flying_data_pipeline_dmr5g_terrain_tests)

add_executable(flying_data_pipeline_pilot_region_package_tests
  data_pipeline/pilot_region_packages_test.cpp
)

target_link_libraries(flying_data_pipeline_pilot_region_package_tests
  PRIVATE
    Flying::DataPipeline
    Flying::CompilerOptions
)

add_test(NAME flying.data_pipeline.pilot_region_packages
  COMMAND flying_data_pipeline_pilot_region_package_tests)

add_executable(flying_data_pipeline_airport_database_tests
  data_pipeline/airport_database_schema_test.cpp
)

target_link_libraries(flying_data_pipeline_airport_database_tests
  PRIVATE
    Flying::DataPipeline
    Flying::CompilerOptions
)

target_compile_definitions(flying_data_pipeline_airport_database_tests
  PRIVATE
    FLYING_REPO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME flying.data_pipeline.airport_database
  COMMAND flying_data_pipeline_airport_database_tests)

add_executable(flying_data_pipeline_runway_importer_tests
  data_pipeline/runway_importer_test.cpp
)

target_link_libraries(flying_data_pipeline_runway_importer_tests
  PRIVATE
    Flying::DataPipeline
    Flying::CompilerOptions
)

target_compile_definitions(flying_data_pipeline_runway_importer_tests
  PRIVATE
    FLYING_REPO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME flying.data_pipeline.runway_importer
  COMMAND flying_data_pipeline_runway_importer_tests)

add_executable(flying_core_sim_kernel_tests
  core_sim/kernel_test.cpp
)

target_link_libraries(flying_core_sim_kernel_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.kernel COMMAND flying_core_sim_kernel_tests)

add_executable(flying_core_sim_flight_dynamics_api_tests
  core_sim/flight_dynamics_api_test.cpp
)

target_link_libraries(flying_core_sim_flight_dynamics_api_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.flight_dynamics_api COMMAND flying_core_sim_flight_dynamics_api_tests)

add_executable(flying_core_sim_aircraft_config_tests
  core_sim/aircraft_config_test.cpp
)

target_link_libraries(flying_core_sim_aircraft_config_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

target_compile_definitions(flying_core_sim_aircraft_config_tests
  PRIVATE
    FLYING_REPO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME flying.core_sim.aircraft_config COMMAND flying_core_sim_aircraft_config_tests)

add_executable(flying_core_sim_aircraft_systems_tests
  core_sim/aircraft_systems_test.cpp
)

target_link_libraries(flying_core_sim_aircraft_systems_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.aircraft_systems COMMAND flying_core_sim_aircraft_systems_tests)

add_executable(flying_core_sim_input_mapping_tests
  core_sim/input_mapping_test.cpp
)

target_link_libraries(flying_core_sim_input_mapping_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.input_mapping COMMAND flying_core_sim_input_mapping_tests)

add_executable(flying_core_sim_scenario_start_tests
  core_sim/scenario_start_test.cpp
)

target_link_libraries(flying_core_sim_scenario_start_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.scenario_start COMMAND flying_core_sim_scenario_start_tests)

add_executable(flying_core_sim_telemetry_replay_tests
  core_sim/telemetry_replay_test.cpp
)

target_link_libraries(flying_core_sim_telemetry_replay_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.telemetry_replay COMMAND flying_core_sim_telemetry_replay_tests)

add_executable(flying_core_sim_terrain_contact_tests
  core_sim/terrain_contact_test.cpp
)

target_link_libraries(flying_core_sim_terrain_contact_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.terrain_contact COMMAND flying_core_sim_terrain_contact_tests)

add_executable(flying_core_sim_weather_tests
  core_sim/weather_test.cpp
)

target_link_libraries(flying_core_sim_weather_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.weather COMMAND flying_core_sim_weather_tests)

add_executable(flying_core_sim_performance_timing_tests
  core_sim/performance_timing_test.cpp
)

target_link_libraries(flying_core_sim_performance_timing_tests
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)

add_test(NAME flying.core_sim.performance_timing
  COMMAND flying_core_sim_performance_timing_tests)

if(TARGET Flying::CoreSimJsbsim)
  add_executable(flying_core_sim_jsbsim_adapter_tests
    core_sim/jsbsim_adapter_test.cpp
  )

  target_link_libraries(flying_core_sim_jsbsim_adapter_tests
    PRIVATE
      Flying::CoreSimJsbsim
      Flying::CompilerOptions
  )

  add_test(NAME flying.core_sim.jsbsim_adapter COMMAND flying_core_sim_jsbsim_adapter_tests)
endif()

add_executable(flying_geo_terrain_geodesy_units_tests
  geo_terrain/geodesy_units_test.cpp
)

target_link_libraries(flying_geo_terrain_geodesy_units_tests
  PRIVATE
    Flying::GeoTerrain
    Flying::CompilerOptions
)

add_test(NAME flying.geo_terrain.geodesy_units COMMAND flying_geo_terrain_geodesy_units_tests)

add_executable(flying_geo_terrain_terrain_service_tests
  geo_terrain/terrain_service_test.cpp
)

target_link_libraries(flying_geo_terrain_terrain_service_tests
  PRIVATE
    Flying::GeoTerrain
    Flying::CompilerOptions
)

add_test(NAME flying.geo_terrain.terrain_service COMMAND flying_geo_terrain_terrain_service_tests)

add_executable(flying_m1_vertical_slice_tests
  m1/vertical_slice_validation_test.cpp
)

target_link_libraries(flying_m1_vertical_slice_tests
  PRIVATE
    Flying::CoreSim
    Flying::GeoTerrain
    Flying::CompilerOptions
)

target_compile_definitions(flying_m1_vertical_slice_tests
  PRIVATE
    FLYING_REPO_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

add_test(NAME flying.m1.vertical_slice COMMAND flying_m1_vertical_slice_tests)

set_tests_properties(
  flying.core_sim.kernel
  flying.core_sim.flight_dynamics_api
  flying.core_sim.input_mapping
  flying.core_sim.scenario_start
  flying.core_sim.telemetry_replay
  flying.core_sim.terrain_contact
  flying.geo_terrain.geodesy_units
  flying.geo_terrain.terrain_service
  flying.data_pipeline.dmr5g_terrain
  flying.data_pipeline.runway_importer
  flying.data_pipeline.pilot_region_packages
  flying.m1.vertical_slice
  PROPERTIES
    LABELS "m1-vertical-slice"
)
