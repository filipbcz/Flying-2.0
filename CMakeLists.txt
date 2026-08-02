cmake_minimum_required(VERSION 3.25)

project(Flying
  VERSION 0.1.0
  DESCRIPTION "Offline-first Win64 flight simulator repository skeleton"
  LANGUAGES CXX
)

option(FLYING_BUILD_TESTS "Build Flying native tests." ON)
option(FLYING_RUNTIME_NETWORK "Allow runtime network-dependent code paths." OFF)
option(FLYING_REQUIRE_MAP_API_KEYS "Require external map API keys or runtime credentials." OFF)
option(FLYING_PACKAGE_WIN64 "Enable Win64 packaging-preparation targets." OFF)

if(FLYING_RUNTIME_NETWORK OR FLYING_REQUIRE_MAP_API_KEYS)
  message(FATAL_ERROR
    "The build skeleton is offline-first and must not require runtime network "
    "credentials or external map APIs.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(FlyingCompilerOptions)

add_subdirectory(geo_terrain)
add_subdirectory(core_sim)
add_subdirectory(data_pipeline)

if(FLYING_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(FLYING_PACKAGE_WIN64)
  add_custom_target(flying_win64_package_manifest
    COMMAND "${CMAKE_COMMAND}" -E echo
            "Win64 packaging preset is configured; installer/signing implementation is a later milestone."
    VERBATIM
  )
endif()
