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

${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror \
    -I"$project_dir/lib/pico_core/include" \
    "$project_dir/tests/host/tracking_policy_tests.cpp" \
    -o "$build_dir/tracking_policy_tests"
"$build_dir/tracking_policy_tests"
printf '%s\n' 'Tracking policy checks passed (live alert routing remains gated)'

python3 "$project_dir/tests/host/test_combined_report.py"

${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror \
    -I"$project_dir/lib/pico_core/include" \
    "$project_dir"/lib/pico_core/src/*.cpp "$project_dir/tests/host/clock_tests.cpp" \
    -o "$build_dir/clock_tests"
"$build_dir/clock_tests"

${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror \
    -I"$project_dir/lib/pico_core/include" \
    "$project_dir"/lib/pico_core/src/*.cpp "$project_dir/tests/host/bond_recovery_tests.cpp" \
    -o "$build_dir/bond_recovery_tests"
"$build_dir/bond_recovery_tests"
