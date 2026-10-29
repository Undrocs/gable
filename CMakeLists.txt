cmake_minimum_required(VERSION 3.20)

project(
  gable
  VERSION 0.3.0
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

add_library(gable_warnings INTERFACE)
if(MSVC)
  target_compile_options(gable_warnings INTERFACE /W4 /utf-8 /Zc:__cplusplus)
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
# GCC 12 reports -Wrestrict false positives inside libstdc++'s char_traits when
# strings are concatenated at -O2 and above, which makes warning-free Release
# builds impossible. The warning never points at Gable code, so it is disabled
# for GCC only; Clang does not implement it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(gable_warnings INTERFACE -Wno-restrict)
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
  runtime/src/args.c
  runtime/src/fs.c
  runtime/src/gc.c
  runtime/src/list.c
  runtime/src/stack_top.c
  runtime/src/io.c
  runtime/src/string.c
  runtime/src/trap.c
)
target_include_directories(gable_runtime PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/runtime/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(gable_runtime PROPERTIES OUTPUT_NAME gable_rt)
if(MSVC)
  # The runtime is linked into programs that the compiler driver builds with
  # cl, so both sides use the static C runtime; mixing the static and DLL
  # flavours fails at link time with unresolved __imp_* symbols.
  set_property(TARGET gable_runtime PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# Host-specific process control and platform services.
if(WIN32)
  set(GABLE_PLATFORM_SOURCES
    compiler/src/platform/windows.cpp
    compiler/src/driver/process_windows.cpp
  )
else()
  set(GABLE_PLATFORM_SOURCES
    compiler/src/platform/posix.cpp
    compiler/src/driver/process_posix.cpp
  )
endif()
target_compile_options(gable_runtime PRIVATE
  $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic>
)
if(MSVC)
  # The runtime uses fopen/fwrite-style APIs on purpose; they are the portable
  # subset of the C library across every host we support.
  target_compile_definitions(gable_runtime PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Static library file names follow the platform's convention.
if(WIN32)
  set(GABLE_INSTALL_RUNTIME_LIB_NAME "${CMAKE_INSTALL_FULL_LIBDIR}/gable_rt.lib")
else()
  set(GABLE_INSTALL_RUNTIME_LIB_NAME "${CMAKE_INSTALL_FULL_LIBDIR}/libgable_rt.a")
endif()

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
  compiler/src/format/format.cpp
  compiler/src/driver/frontend.cpp
  compiler/src/driver/loader.cpp
  compiler/src/driver/output.cpp
  compiler/src/driver/temporary.cpp
  ${GABLE_PLATFORM_SOURCES}
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
  GABLE_INSTALL_RUNTIME_LIB="${GABLE_INSTALL_RUNTIME_LIB_NAME}"
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
    tests/unit/diagnostic_tests.cpp
    tests/unit/format_tests.cpp
    tests/unit/robustness_tests.cpp
  )
  target_link_libraries(gable_unit_tests PRIVATE gable_compiler)
  target_include_directories(gable_unit_tests PRIVATE tests/unit)
  add_test(NAME unit COMMAND gable_unit_tests)

  # The notes tool is the acceptance test: a real multi-file program that
  # is built and driven through its command line.
  add_executable(notes_tool_tests tools/notes/tests/notes_tool_tests.cpp)
  target_link_libraries(notes_tool_tests PRIVATE gable_compiler)
  add_test(NAME notes_tool
    COMMAND notes_tool_tests
      $<TARGET_FILE:gable>
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/notes/notes.gable
  )
  set_tests_properties(notes_tool PROPERTIES TIMEOUT 300)

  # The runtime's file builtins are tested directly in C: a Gable program cannot
  # build a path with a NUL byte or a file above the size limit. The test needs
  # fork(), chmod() and ftruncate() and has not been exercised on Windows, so it
  # is not registered there.
  if(NOT WIN32)
    add_executable(fs_tests runtime/tests/fs_tests.c)
    target_link_libraries(fs_tests PRIVATE gable_runtime)
    add_test(NAME runtime_files COMMAND fs_tests)
    set_tests_properties(runtime_files PROPERTIES TIMEOUT 120)
  endif()

  # Memory reclamation is checked directly against the runtime: a Gable program
  # can show that live values survive, but not that unreachable ones are freed.
  add_executable(gc_tests runtime/tests/gc_tests.c)
  target_link_libraries(gc_tests PRIVATE gable_runtime)
  target_include_directories(gc_tests PRIVATE runtime/src)
  add_test(NAME runtime_memory COMMAND gc_tests)
  set_tests_properties(runtime_memory PROPERTIES TIMEOUT 120)

  add_executable(list_tests runtime/tests/list_tests.c)
  target_link_libraries(list_tests PRIVATE gable_runtime)
  target_include_directories(list_tests PRIVATE runtime/src)
  add_test(NAME runtime_lists COMMAND list_tests)
  set_tests_properties(runtime_lists PROPERTIES TIMEOUT 120)

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
