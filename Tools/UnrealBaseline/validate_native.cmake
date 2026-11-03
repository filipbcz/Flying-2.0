cmake_minimum_required(VERSION 3.25)

# The leased worker's restricted validator cannot reliably initialize MSBuild's
# FileTracker (MSB4018 / E_ACCESSDENIED). Makefile generators retain dependency
# tracking without loading that component. Leave the general test preset unchanged.
get_filename_component(repo_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(build_dir "${repo_root}/out/build/editor-baseline-native")
file(MAKE_DIRECTORY "${build_dir}")

if(CMAKE_HOST_WIN32)
  # Select x64 explicitly; the worker's ordinary PATH may contain x86 cl.exe.
  cmake_host_system_information(RESULT vs_root QUERY VS_17_DIR)
  set(vsdevcmd "${vs_root}/Common7/Tools/VsDevCmd.bat")
  if(NOT EXISTS "${vsdevcmd}")
    message(FATAL_ERROR "Visual Studio 2022 C++ tools are required: ${vsdevcmd}")
  endif()
  file(TO_NATIVE_PATH "${vsdevcmd}" vsdevcmd_native)
  set(bootstrap "${build_dir}/msvc-environment.cmd")
  file(WRITE "${bootstrap}"
    "@echo off\r\nset VSCMD_SKIP_SENDTELEMETRY=1\r\ncall \"${vsdevcmd_native}\" -no_logo -arch=x64 -host_arch=x64\r\nif errorlevel 1 exit /b %errorlevel%\r\nset\r\n")
  execute_process(
    COMMAND "$ENV{COMSPEC}" /d /c "${bootstrap}"
    RESULT_VARIABLE env_result OUTPUT_VARIABLE msvc_environment
    ERROR_VARIABLE env_error)
  if(NOT env_result STREQUAL "0")
    message(FATAL_ERROR "MSVC environment setup failed (${env_result}):\n${env_error}\n${msvc_environment}")
  endif()
  # Match whole values rather than splitting on semicolons in PATH/INCLUDE/LIB.
  foreach(name IN ITEMS PATH INCLUDE LIB LIBPATH)
    string(REGEX MATCH "(^|[\r\n])${name}=([^\r\n]*)" match "${msvc_environment}")
    if(NOT match)
      message(FATAL_ERROR "MSVC environment did not define ${name}")
    endif()
    set(ENV{${name}} "${CMAKE_MATCH_2}")
  endforeach()
  find_program(msvc_compiler NAMES cl.exe REQUIRED NO_CACHE)
  set(compiler_option "-DCMAKE_CXX_COMPILER=${msvc_compiler}")
  set(native_generator "NMake Makefiles")
  find_program(make_program NAMES nmake.exe REQUIRED NO_CACHE)
else()
  set(native_generator "Unix Makefiles")
  find_program(make_program NAMES gmake make REQUIRED NO_CACHE)
endif()
get_filename_component(cmake_bin_dir "${CMAKE_COMMAND}" DIRECTORY)
find_program(ctest_program NAMES ctest HINTS "${cmake_bin_dir}" REQUIRED NO_CACHE)

function(run_required)
  execute_process(COMMAND ${ARGV} WORKING_DIRECTORY "${repo_root}"
    COMMAND_ECHO STDOUT RESULT_VARIABLE result TIMEOUT 600)
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "Native baseline validation failed (${result}): ${ARGV0}")
  endif()
endfunction()

# A separate cache avoids changing the existing Visual Studio build directory.
run_required("${CMAKE_COMMAND}" --preset test --fresh -G "${native_generator}" -B "${build_dir}"
  "-DCMAKE_MAKE_PROGRAM=${make_program}"
  -DCMAKE_POLICY_DEFAULT_CMP0141=NEW -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded
  ${compiler_option})
run_required("${CMAKE_COMMAND}" --build "${build_dir}" --parallel 1 --target
  flying_core_sim_kernel_tests
  flying_core_sim_determinism_tests
  flying_core_sim_flight_dynamics_api_tests
  flying_core_sim_aircraft_systems_tests
  flying_core_sim_input_mapping_tests
  flying_core_sim_scenario_start_tests
  flying_core_sim_telemetry_replay_tests
  flying_geo_terrain_geodesy_units_tests)
run_required("${ctest_program}" --test-dir "${build_dir}" -C Debug
  --output-on-failure --no-tests=error -R
  "^flying\\.(core_sim\\.(kernel|determinism|flight_dynamics_api|aircraft_systems|input_mapping|scenario_start|telemetry_replay)|geo_terrain\\.geodesy_units)$")
