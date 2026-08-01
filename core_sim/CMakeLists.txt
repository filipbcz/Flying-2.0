add_library(flying_core_sim STATIC
  src/module.cpp
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
