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
