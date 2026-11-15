cmake_minimum_required(VERSION 3.20)
project(Apex VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Target ISA: x86-64-v3 (AVX2/BMI2) by default; "native", "v4" (AVX-512) or "v2" are alternatives.
set(APEX_ARCH "v3" CACHE STRING "Target ISA level: v2, v3, v4 or native")

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus)
  if(APEX_ARCH STREQUAL "v3")
    add_compile_options(/arch:AVX2)
  elseif(APEX_ARCH STREQUAL "v4")
    add_compile_options(/arch:AVX512)
  endif()
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " /O2 /Ob3 /GL")
  string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /LTCG")
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(APEX_ARCH STREQUAL "native")
    add_compile_options(-march=native)
  elseif(APEX_ARCH STREQUAL "v4")
    add_compile_options(-march=x86-64-v4)
  elseif(APEX_ARCH STREQUAL "v3")
    add_compile_options(-march=x86-64-v3)
  else()
    add_compile_options(-march=x86-64-v2)
  endif()
  string(APPEND CMAKE_CXX_FLAGS_RELEASE " -O3 -fno-plt")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
endif()

option(APEX_SANITIZE "Build with address/undefined sanitizers" OFF)
if(APEX_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)

add_library(apex_core STATIC
  src/core/bitboard.cpp
  src/core/position.cpp
  src/core/movegen.cpp
  src/eval/eval.cpp
  src/eval/nnue/nnue.cpp
  src/search/tt.cpp
  src/search/search.cpp
  src/uci/uci.cpp
  src/util/cpuinfo.cpp
  src/tools/datagen.cpp
)
target_include_directories(apex_core PUBLIC src)
target_link_libraries(apex_core PUBLIC Threads::Threads)

add_executable(apex src/main.cpp)
target_link_libraries(apex PRIVATE apex_core)

add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE apex_core)

if(ipo_ok AND CMAKE_BUILD_TYPE STREQUAL "Release")
  set_property(TARGET apex_core apex test_core PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

enable_testing()
add_test(NAME core COMMAND test_core)
