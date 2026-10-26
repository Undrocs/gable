cmake_minimum_required(VERSION 3.20)

project(
  gable
  VERSION 0.1.0
  DESCRIPTION "The Gable programming language compiler"
  HOMEPAGE_URL "https://github.com/Undrocs/gable"
  LANGUAGES C CXX
)

# Keep the default build type predictable for developers who configure without
# a preset.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CTest)

option(GABLE_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GABLE_WARNINGS_AS_ERRORS "Treat compiler warnings in Gable's own code as errors" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  message(FATAL_ERROR
    "Gable does not support Windows hosts yet. The compiler driver currently relies on POSIX "
    "process control (fork/exec). See docs/roadmap.md.")
endif()

add_library(gable_warnings INTERFACE)
if(MSVC)
  target_compile_options(gable_warnings INTERFACE /W4)
else()
  target_compile_options(gable_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
  )
endif()
if(GABLE_WARNINGS_AS_ERRORS)
  if(MSVC)
    target_compile_options(gable_warnings INTERFACE /WX)
  else()
    target_compile_options(gable_warnings INTERFACE -Werror)
  endif()
endif()

# ---------------------------------------------------------------------------
# Runtime (C17)
# ---------------------------------------------------------------------------
add_library(gable_runtime STATIC
  runtime/src/io.c
  runtime/src/trap.c
)
target_include_directories(gable_runtime PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(gable_runtime PROPERTIES OUTPUT_NAME gable_rt)
target_compile_options(gable_runtime PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
)

# ---------------------------------------------------------------------------
# Compiler library (C++20)
# ---------------------------------------------------------------------------
add_library(gable_compiler STATIC
  compiler/src/lexer/lexer.cpp
  compiler/src/lexer/source.cpp
  compiler/src/parser/parser.cpp
  compiler/src/semantic/sema.cpp
  compiler/src/semantic/diagnostics.cpp
  compiler/src/types/ir.cpp
  compiler/src/types/operators.cpp
  compiler/src/codegen/c_backend.cpp
  compiler/src/driver/frontend.cpp
  compiler/src/driver/paths.cpp
  compiler/src/driver/process.cpp
  compiler/src/driver/temporary.cpp
  compiler/src/driver/toolchain.cpp
  compiler/src/cli/cli.cpp
  compiler/src/cli/version.cpp
)
target_include_directories(gable_compiler PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/compiler/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(gable_compiler PUBLIC gable_warnings)
target_compile_definitions(gable_compiler PRIVATE
  GABLE_VERSION_STRING="${PROJECT_VERSION}"
  # Defaults used when the caller does not set GABLE_RUNTIME_INCLUDE/LIB.
  # The build-tree paths are preferred while the files exist; installed builds
  # fall back to the install-tree paths below.
  GABLE_BUILD_RUNTIME_INCLUDE="${CMAKE_CURRENT_SOURCE_DIR}/runtime/include"
  GABLE_BUILD_RUNTIME_LIB="$<TARGET_FILE:gable_runtime>"
  GABLE_INSTALL_RUNTIME_INCLUDE="${CMAKE_INSTALL_FULL_INCLUDEDIR}"
  GABLE_INSTALL_RUNTIME_LIB="${CMAKE_INSTALL_FULL_LIBDIR}/libgable_rt.a"
)

if(GABLE_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "GABLE_ENABLE_SANITIZERS requires Clang or GCC")
  endif()
  target_compile_options(gable_compiler PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(gable_compiler PUBLIC -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Compiler executable
# ---------------------------------------------------------------------------
add_executable(gable compiler/src/cli/main.cpp)
target_link_libraries(gable gable_compiler)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  enable_testing()

  add_executable(gable_unit_tests
    tests/unit/main.cpp
    tests/unit/lexer_tests.cpp
    tests/unit/parser_tests.cpp
    tests/unit/semantic_tests.cpp
    tests/unit/codegen_tests.cpp
    tests/unit/cli_tests.cpp
    tests/unit/robustness_tests.cpp
  )
  target_link_libraries(gable_unit_tests PRIVATE gable_compiler)
  target_include_directories(gable_unit_tests PRIVATE tests/unit)
  add_test(NAME unit COMMAND gable_unit_tests)

  add_executable(gable_integration_tests tests/integration/integration_tests.cpp)
  target_link_libraries(gable_integration_tests PRIVATE gable_compiler)
  add_test(NAME integration
    COMMAND gable_integration_tests
      $<TARGET_FILE:gable>
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/cases
      ${CMAKE_CURRENT_SOURCE_DIR}/examples
  )
  # The integration suite compiles and runs real programs; keep it sequential.
  set_tests_properties(integration PROPERTIES TIMEOUT 300)
endif()

# ---------------------------------------------------------------------------
# Installation
# ---------------------------------------------------------------------------
install(TARGETS gable RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS gable_runtime ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY runtime/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# The compiler library is not installed: its interface is not stable yet, and
# nothing outside this repository consumes it. Only the runtime and the CLI are.
install(DIRECTORY docs DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES README.md LICENSE DESTINATION ${CMAKE_INSTALL_DOCDIR})
