#!/usr/bin/env bash
# Build without CMake.
#
# Some development hosts (including the sandbox this work was validated in)
# have no CMake and no Ninja. This script reproduces the build that
# CMakeLists.txt describes: the C17 runtime as a static library, the C++20
# compiler library, the CLI, and the test binaries. The supported build
# instructions remain the ones in docs/getting-started.md.
#
#   tools/build-without-cmake.sh [build-dir] [--sanitize] [--release]
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
out=${1:-"$root/build/manual"}
shift || true

mode=debug
sanitize=
for argument in "$@"; do
  case "$argument" in
    --sanitize) sanitize=1 ;;
    --release) mode=release ;;
    *) echo "unknown argument: $argument" >&2; exit 2 ;;
  esac
done

cxx=${CXX:-g++}
cc=${CC:-gcc}
# The compile-time defaults can be overridden so that a build can be produced whose
# recorded paths are deliberately wrong; tools/check-relocated-install.sh uses that
# to verify that a relocated compiler finds its runtime next to its own binary.
build_runtime_include=${GABLE_BUILD_RUNTIME_INCLUDE:-"$root/runtime/include"}
build_runtime_lib=${GABLE_BUILD_RUNTIME_LIB:-"$out/libgable_rt.a"}
install_runtime_include=${GABLE_INSTALL_RUNTIME_INCLUDE:-/usr/local/include}
install_runtime_lib=${GABLE_INSTALL_RUNTIME_LIB:-/usr/local/lib/libgable_rt.a}
common=(-std=c++20 -pthread -I"$root/compiler/include" -Wall -Wextra -Wpedantic -Wshadow
        -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast
        -DGABLE_VERSION_STRING=\"0.2.0\"
        -DGABLE_BUILD_RUNTIME_INCLUDE=\"$build_runtime_include\"
        -DGABLE_BUILD_RUNTIME_LIB=\"$build_runtime_lib\"
        -DGABLE_INSTALL_RUNTIME_INCLUDE=\"$install_runtime_include\"
        -DGABLE_INSTALL_RUNTIME_LIB=\"$install_runtime_lib\")
if [[ $mode == release ]]; then
  common+=(-O2 -DNDEBUG)
else
  common+=(-O0 -g)
fi
if [[ -n $sanitize ]]; then
  common+=(-fsanitize=address,undefined -fno-omit-frame-pointer)
fi

mkdir -p "$out"

echo "== runtime"
runtime_flags=(-std=c17 -O2 -Wall -Wextra -Wpedantic -I"$root/runtime/include")
test_flags=(-std=c17 -O1 -Wall -Wextra -Wpedantic -I"$root/runtime/include")
if [[ -n $sanitize ]]; then
  # The runtime is linked into every generated program, so a sanitizer build of
  # the compiler is only meaningful when the runtime is instrumented too.
  runtime_flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  test_flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
fi
runtime_objects=()
for source in "$root"/runtime/src/*.c; do
  object="$out/runtime-$(basename "${source%.c}").o"
  "$cc" "${runtime_flags[@]}" -c "$source" -o "$object"
  runtime_objects+=("$object")
done
ar rcs "$out/libgable_rt.a" "${runtime_objects[@]}"

echo "== compiler"
objects=()
for source in "$root"/compiler/src/*/*.cpp; do
  case "$(basename "$source")" in
    main.cpp) continue ;;
    process_windows.cpp | windows.cpp) continue ;;
  esac
  object="$out/$(basename "${source%.cpp}").o"
  "$cxx" "${common[@]}" -c "$source" -o "$object"
  objects+=("$object")
done
"$cxx" "${common[@]}" -c "$root/compiler/src/cli/main.cpp" -o "$out/main.o"
"$cxx" "${common[@]}" "$out/main.o" "${objects[@]}" -o "$out/gable"

echo "== tests"
"$cxx" "${common[@]}" -I"$root/tests/unit" \
  "$root/tests/unit/"*.cpp "${objects[@]}" -o "$out/gable_unit_tests"
"$cxx" "${common[@]}" \
  "$root/tests/integration/integration_tests.cpp" "${objects[@]}" \
  -o "$out/gable_integration_tests"
"$cxx" "${common[@]}" \
  "$root/tools/notes/tests/notes_tool_tests.cpp" "${objects[@]}" \
  -o "$out/notes_tool_tests"
"$cc" "${test_flags[@]}" \
  "$root/runtime/tests/fs_tests.c" "$out/libgable_rt.a" \
  -o "$out/fs_tests"

echo "== done: $out"
