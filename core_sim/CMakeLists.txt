add_library(flying_core_sim STATIC
  src/determinism.cpp
  src/flight_dynamics.cpp
  src/fixed_step.cpp
  src/input.cpp
  src/module.cpp
  src/scenario.cpp
  src/simulator.cpp
  src/telemetry.cpp
  src/terrain_contact.cpp
)

add_library(Flying::CoreSim ALIAS flying_core_sim)

target_include_directories(flying_core_sim
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

target_link_libraries(flying_core_sim
  PUBLIC
    Flying::GeoTerrain
    Flying::CompilerOptions
)

target_compile_definitions(flying_core_sim
  PUBLIC
    FLYING_CORE_SIM_VERSION="${PROJECT_VERSION}"
)

option(FLYING_CORE_SIM_ENABLE_JSBSIM "Enable the JSBSim CoreSim adapter when JSBSim is available." ON)
option(FLYING_CORE_SIM_FETCH_JSBSIM "Fetch the pinned JSBSim dependency when it is not installed." OFF)
option(FLYING_CORE_SIM_REQUIRE_JSBSIM "Fail configuration if the JSBSim adapter cannot be built." OFF)
set(FLYING_JSBSIM_GIT_REPOSITORY "https://github.com/JSBSim-Team/jsbsim.git"
    CACHE STRING "Pinned JSBSim upstream repository.")
set(FLYING_JSBSIM_GIT_TAG "570e8115a102df8f877b11e0e59b964ea483e3c0"
    CACHE STRING "Pinned JSBSim v1.2.3 tag commit for CoreSim infrastructure tests.")

set(flying_jsbsim_target)

function(flying_select_jsbsim_target out_var)
  foreach(candidate IN ITEMS
      JSBSim::JSBSim
      JSBSim::libJSBSim
      jsbsim::jsbsim
      JSBSIM::JSBSIM
      libJSBSim)
    if(TARGET "${candidate}")
      set("${out_var}" "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()

  set("${out_var}" "" PARENT_SCOPE)
endfunction()

function(flying_select_jsbsim_variable_target out_var)
  foreach(prefix IN ITEMS JSBSIM JSBSim jsbsim)
    set(found_var "${prefix}_FOUND")
    if(DEFINED ${found_var} AND NOT "${${found_var}}")
      continue()
    endif()

    set(libraries)
    foreach(library_var IN ITEMS "${prefix}_LIBRARIES" "${prefix}_LIBRARY")
      if(DEFINED ${library_var})
        set(libraries "${${library_var}}")
        break()
      endif()
    endforeach()
    if(NOT libraries)
      continue()
    endif()

    set(includes)
    foreach(include_var IN ITEMS "${prefix}_INCLUDE_DIRS" "${prefix}_INCLUDE_DIR")
      if(DEFINED ${include_var})
        set(includes "${${include_var}}")
        break()
      endif()
    endforeach()

    if(NOT TARGET flying_jsbsim_from_variables)
      add_library(flying_jsbsim_from_variables INTERFACE IMPORTED)
      set_target_properties(flying_jsbsim_from_variables PROPERTIES
        INTERFACE_LINK_LIBRARIES "${libraries}"
      )
      if(includes)
        set_target_properties(flying_jsbsim_from_variables PROPERTIES
          INTERFACE_INCLUDE_DIRECTORIES "${includes}"
        )
      endif()
    endif()

    set("${out_var}" flying_jsbsim_from_variables PARENT_SCOPE)
    return()
  endforeach()

  set("${out_var}" "" PARENT_SCOPE)
endfunction()

if(FLYING_CORE_SIM_ENABLE_JSBSIM)
  find_package(JSBSim CONFIG QUIET NAMES JSBSim jsbsim JSBSIM)
  flying_select_jsbsim_target(flying_jsbsim_target)

  if(NOT flying_jsbsim_target)
    flying_select_jsbsim_variable_target(flying_jsbsim_target)
  endif()

  if(NOT flying_jsbsim_target)
    find_package(JSBSim MODULE QUIET)
    flying_select_jsbsim_target(flying_jsbsim_target)
  endif()

  if(NOT flying_jsbsim_target)
    flying_select_jsbsim_variable_target(flying_jsbsim_target)
  endif()

  if(NOT flying_jsbsim_target)
    find_package(JSBSIM MODULE QUIET)
    flying_select_jsbsim_target(flying_jsbsim_target)
  endif()

  if(NOT flying_jsbsim_target)
    flying_select_jsbsim_variable_target(flying_jsbsim_target)
  endif()

  if(NOT flying_jsbsim_target AND FLYING_CORE_SIM_FETCH_JSBSIM)
    include(FetchContent)
    FetchContent_Declare(flying_jsbsim
      GIT_REPOSITORY "${FLYING_JSBSIM_GIT_REPOSITORY}"
      GIT_TAG "${FLYING_JSBSIM_GIT_TAG}"
      GIT_SHALLOW FALSE
    )

    set(BUILD_DOCS OFF CACHE BOOL "Disable JSBSim documentation in Flying builds." FORCE)
    set(BUILD_JULIA_PACKAGE OFF CACHE BOOL "Disable JSBSim Julia package in Flying builds." FORCE)
    set(BUILD_MATLAB_SFUNCTION OFF CACHE BOOL "Disable JSBSim MATLAB package in Flying builds." FORCE)
    set(BUILD_PYTHON_MODULE OFF CACHE BOOL "Disable JSBSim Python package in Flying builds." FORCE)
    set(INSTALL_JSBSIM_PYTHON_MODULE OFF CACHE BOOL "Disable JSBSim Python install in Flying builds." FORCE)

    FetchContent_GetProperties(flying_jsbsim)
    if(NOT flying_jsbsim_POPULATED)
      FetchContent_Populate(flying_jsbsim)
      if(UNIX AND NOT DEFINED LSB_RELEASE_EXECUTABLE)
        find_program(LSB_RELEASE_EXECUTABLE lsb_release)
      endif()
      add_subdirectory("${flying_jsbsim_SOURCE_DIR}" "${flying_jsbsim_BINARY_DIR}" EXCLUDE_FROM_ALL SYSTEM)
    endif()

    flying_select_jsbsim_target(flying_jsbsim_target)
  endif()
endif()

if(flying_jsbsim_target)
  add_library(flying_core_sim_jsbsim STATIC
    src/jsbsim_adapter.cpp
  )

  add_library(Flying::CoreSimJsbsim ALIAS flying_core_sim_jsbsim)

  target_link_libraries(flying_core_sim_jsbsim
    PUBLIC
      Flying::CoreSim
    PRIVATE
      "${flying_jsbsim_target}"
  )

  target_compile_definitions(flying_core_sim_jsbsim
    PRIVATE
      FLYING_CORE_SIM_JSBSIM_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/jsbsim"
  )
elseif(FLYING_CORE_SIM_REQUIRE_JSBSIM)
  message(FATAL_ERROR
    "FLYING_CORE_SIM_REQUIRE_JSBSIM is ON, but JSBSim was not found. "
    "Install JSBSim development files, set JSBSIM_INCLUDE_DIR/JSBSIM_LIBRARY, "
    "or configure with FLYING_CORE_SIM_FETCH_JSBSIM=ON.")
elseif(NOT FLYING_CORE_SIM_ENABLE_JSBSIM)
  message(STATUS "JSBSim adapter target disabled by FLYING_CORE_SIM_ENABLE_JSBSIM=OFF.")
else()
  message(STATUS
    "JSBSim adapter target disabled because JSBSim was not found. "
    "Set FLYING_CORE_SIM_FETCH_JSBSIM=ON or provide JSBSim development files to enable it.")
endif()

add_executable(flying_core_sim_headless
  tools/headless_runner.cpp
)

target_link_libraries(flying_core_sim_headless
  PRIVATE
    Flying::CoreSim
    Flying::CompilerOptions
)
