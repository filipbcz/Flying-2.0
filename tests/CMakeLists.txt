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
