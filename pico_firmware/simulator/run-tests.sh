#!/usr/bin/env sh
set -eu

simulator_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$simulator_dir/tests/test_backend.py"
