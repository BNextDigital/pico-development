#!/usr/bin/env sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror \
    -I"$project_dir/lib/pico_core/include" \
    -include "$project_dir/tests/host/nordic_limits.hpp" -fsyntax-only \
    "$project_dir"/lib/pico_core/src/*.cpp
printf '%s\n' 'Nordic UINT32_MAX type regression check passed'
