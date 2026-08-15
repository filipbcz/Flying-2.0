add_library(flying_telemetry_replay INTERFACE)

add_library(Flying::TelemetryReplay ALIAS flying_telemetry_replay)

target_include_directories(flying_telemetry_replay
  INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Public>"
)

target_link_libraries(flying_telemetry_replay
  INTERFACE
    Flying::CoreSim
)
