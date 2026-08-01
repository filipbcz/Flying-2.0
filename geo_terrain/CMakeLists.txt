add_library(flying_geo_terrain STATIC
  src/module.cpp
)

add_library(Flying::GeoTerrain ALIAS flying_geo_terrain)

target_include_directories(flying_geo_terrain
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(flying_geo_terrain
  PUBLIC
    Flying::CompilerOptions
)
