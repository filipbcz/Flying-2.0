cmake_minimum_required(VERSION 3.25)

get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(FLYING_NATIVE_BUILD_DIR "${repo_root}/out/build/scenario-package-gate-native")
set(FLYING_NATIVE_CONFIGURE_OPTIONS -DFLYING_CORE_SIM_ENABLE_JSBSIM=OFF)
set(FLYING_NATIVE_TARGETS flying_core_sim_scenario_start_tests)
set(FLYING_NATIVE_TEST_REGEX "^flying\\.core_sim\\.scenario_start$")
include("${CMAKE_CURRENT_LIST_DIR}/../UnrealBaseline/validate_native.cmake")
