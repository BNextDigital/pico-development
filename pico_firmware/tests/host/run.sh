#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir="$project_dir/build-host"

mkdir -p "$build_dir"

set -- "$project_dir"/lib/pico_core/src/*.cpp

${CXX:-c++} \
    -std=c++17 \
    -Wall -Wextra -Wpedantic -Werror \
    -I"$project_dir/lib/pico_core/include" \
    "$@" \
    "$project_dir/tests/host/core_tests.cpp" \
    -o "$build_dir/pico_core_tests"

"$build_dir/pico_core_tests"

sh "$project_dir/tests/host/check_nordic_limits.sh"

sh "$project_dir/tests/platform/run.sh"

# Isolated network prototype: portable flash-layout and button-policy guards.
sh "$project_dir/experiments/find_hub/tests/run.sh"
