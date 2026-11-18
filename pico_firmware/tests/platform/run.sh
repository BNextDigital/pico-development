#!/usr/bin/env sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$project_dir/build-host"
${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror ${CXXFLAGS:-} \
    -I"$project_dir/tests/platform/fakes" -I"$project_dir/lib/pico_core/include" \
    -I"$project_dir/src" "$project_dir/src/platform/pico_button.cpp" \
    "$project_dir/tests/platform/button_adapter_tests.cpp" \
    -o "$project_dir/build-host/button_adapter_tests"
"$project_dir/build-host/button_adapter_tests"
