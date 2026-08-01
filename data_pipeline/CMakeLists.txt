add_library(flying_data_pipeline STATIC
  src/module.cpp
)

add_library(Flying::DataPipeline ALIAS flying_data_pipeline)

target_include_directories(flying_data_pipeline
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(flying_data_pipeline
  PUBLIC
    Flying::CompilerOptions
)
