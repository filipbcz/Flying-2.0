add_library(flying_data_pipeline STATIC
  src/manifest.cpp
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

add_executable(flying_data_pipeline_cli
  tools/pipeline_cli.cpp
)

set_target_properties(flying_data_pipeline_cli
  PROPERTIES
    OUTPUT_NAME flying-data-pipeline
)

target_link_libraries(flying_data_pipeline_cli
  PRIVATE
    Flying::DataPipeline
    Flying::CompilerOptions
)
