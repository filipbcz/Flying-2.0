add_library(flying_core_sim STATIC
  src/determinism.cpp
  src/fixed_step.cpp
  src/module.cpp
  src/simulator.cpp
)

add_library(Flying::CoreSim ALIAS flying_core_sim)

target_include_directories(flying_core_sim
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(flying_core_sim
  PUBLIC
    Flying::CompilerOptions
)

add_executable(flying_core_sim_headless
  tools/headless_runner.cpp
)

target_link_libraries(flying_core_sim_headless
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)
