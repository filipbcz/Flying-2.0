add_library(flying_data_pipeline STATIC
  src/airport_database.cpp
  src/dmr5g_terrain.cpp
  src/manifest.cpp
  src/module.cpp
  src/pilot_region_packages.cpp
  src/runway_importer.cpp
)

if(NOT MSVC)
  set_source_files_properties(src/dmr5g_terrain.cpp
    PROPERTIES
      COMPILE_OPTIONS "$<$<CONFIG:Debug>:-g1>"
  )
endif()

add_library(Flying::DataPipeline ALIAS flying_data_pipeline)

target_include_directories(flying_data_pipeline
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(flying_data_pipeline
  PUBLIC
    Flying::CompilerOptions
  PRIVATE
    Flying::GeoTerrain
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
