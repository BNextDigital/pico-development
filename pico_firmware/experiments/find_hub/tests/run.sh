#!/usr/bin/env sh
set -eu
probe_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$probe_dir/build-tests"
${PICO_TEST_PYTHON:-python3} -m unittest discover -s "$probe_dir/tests" -v
${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror ${CXXFLAGS:-} \
    "$probe_dir/tests/buttons_test.cpp" -o "$probe_dir/build-tests/buttons_test"
"$probe_dir/build-tests/buttons_test"
