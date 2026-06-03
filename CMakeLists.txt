cmake_minimum_required(VERSION 3.16)
project(generator_anc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# Self-contained executables (no MinGW runtime DLLs required to run/distribute).
if(NOT MSVC)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# Debug builds get sanitizers (GCC/Clang).
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/sim)

add_executable(eoc_tests tests/test_eoc.cpp)
add_executable(genset_sim sim/genset_sim.cpp)
add_executable(replay_recording sim/replay_recording.cpp)

enable_testing()
add_test(NAME eoc_tests COMMAND eoc_tests)
