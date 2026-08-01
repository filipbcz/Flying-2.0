add_library(flying_compiler_options INTERFACE)
add_library(Flying::CompilerOptions ALIAS flying_compiler_options)

target_compile_features(flying_compiler_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(flying_compiler_options INTERFACE /W4 /permissive-)
else()
  target_compile_options(flying_compiler_options INTERFACE -Wall -Wextra -Wpedantic)
endif()
