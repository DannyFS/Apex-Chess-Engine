cmake_minimum_required(VERSION 3.20)
project(ApexNNUE LANGUAGES C CXX)

option(USE_CUDA "Enable CUDA NNUE backend" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (MSVC)
  add_compile_options(/W4 /permissive- /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
endif()

include(CheckCXXCompilerFlag)

set(APEX_CPU_FLAGS "")

if (MSVC)
  add_compile_options(/O2)
  # MSVC doesn't support per-flag checks like gcc/clang in the same way.
else()
  add_compile_options(-O3)
  check_cxx_compiler_flag("-mavx2" HAS_AVX2)
  check_cxx_compiler_flag("-mavx512f" HAS_AVX512)
  check_cxx_compiler_flag("-mbmi2" HAS_BMI2)
  if (HAS_AVX2)
    list(APPEND APEX_CPU_FLAGS "-mavx2")
  endif()
  if (HAS_AVX512)
    list(APPEND APEX_CPU_FLAGS "-mavx512f")
  endif()
  if (HAS_BMI2)
    list(APPEND APEX_CPU_FLAGS "-mbmi2")
  endif()
endif()

add_library(apex_cpu_flags INTERFACE)
if (NOT MSVC)
  target_compile_options(apex_cpu_flags INTERFACE ${APEX_CPU_FLAGS})
endif()

set(SOURCES
  src/main.cpp
  src/uci.cpp
  src/position.cpp
  src/movegen.cpp
  src/search.cpp
  src/tt.cpp
  src/timeman.cpp
  src/util/zobrist.cpp
  src/util/threads.cpp
  src/util/perft.cpp
  src/util/bench.cpp
  src/nnue/nnue_cpu.cpp
  src/nnue/nnue_features.cpp
  src/nnue/nnue_loader.cpp
  src/syzygy/tbprobe.cpp
  src/syzygy/tbprobe_fathom.c
)

if (USE_CUDA)
  enable_language(CUDA)
  set(CMAKE_CUDA_STANDARD 17)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
  list(APPEND SOURCES src/nnue/nnue_gpu.cu)
  add_compile_definitions(APEX_USE_CUDA)
endif()

add_executable(ApexNNUE ${SOURCES})

target_include_directories(ApexNNUE PRIVATE src)

target_link_libraries(ApexNNUE PRIVATE apex_cpu_flags)

# Tests
add_executable(test_perft tests/test_perft.cpp
  src/position.cpp
  src/movegen.cpp
  src/util/bitboard.h
  src/util/zobrist.cpp
  src/nnue/nnue_cpu.cpp
  src/nnue/nnue_features.cpp
  src/nnue/nnue_loader.cpp
)

target_include_directories(test_perft PRIVATE src)

target_link_libraries(test_perft PRIVATE apex_cpu_flags)

add_executable(test_movegen tests/test_movegen.cpp
  src/position.cpp
  src/movegen.cpp
  src/util/bitboard.h
  src/util/zobrist.cpp
)

target_include_directories(test_movegen PRIVATE src)

target_link_libraries(test_movegen PRIVATE apex_cpu_flags)

if (MSVC)
  target_compile_definitions(ApexNNUE PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(test_perft PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(test_movegen PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
