#!/usr/bin/env sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$project_dir/build-host"
${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror ${CXXFLAGS:-} \
    -I"$project_dir/tests/platform/fakes" -I"$project_dir/lib/pico_core/include" \
    -I"$project_dir/src" "$project_dir/src/platform/pico_button.cpp" "$project_dir/src/platform/pico_status_led.cpp" \
    "$project_dir/tests/platform/button_adapter_tests.cpp" \
    -o "$project_dir/build-host/button_adapter_tests"
"$project_dir/build-host/button_adapter_tests"

for profile in enabled absent; do
    display_options=""
    if [ "$profile" = enabled ]; then display_options="-DCONFIG_PICO_DISPLAY=1"; fi
    ${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror ${CXXFLAGS:-} $display_options \
        -I"$project_dir/tests/platform/fakes" -I"$project_dir/lib/pico_core/include" \
        -I"$project_dir/src" "$project_dir/src/platform/pico_display.cpp" \
        "$project_dir/lib/pico_core/src/display_model.cpp" "$project_dir/lib/pico_core/src/pitch.cpp" "$project_dir/lib/pico_core/src/audio.cpp" \
        "$project_dir/tests/platform/display_adapter_tests.cpp" \
        -o "$project_dir/build-host/display_adapter_$profile"
    "$project_dir/build-host/display_adapter_$profile"
done

${CXX:-c++} -std=c++17 -Wall -Wextra -Wpedantic -Werror ${CXXFLAGS:-} \
    -DCONFIG_PICO_AUDIO_I2S=1 -DCONFIG_PICO_AUDIO_VOLUME_PERCENT=25 \
    -I"$project_dir/tests/platform/audio_fakes" -I"$project_dir/tests/platform/fakes" \
    -I"$project_dir/lib/pico_core/include" -I"$project_dir/src" \
    "$project_dir/tests/platform/audio_reference_tests.cpp" \
    "$project_dir/lib/pico_core/src/audio.cpp" -o "$project_dir/build-host/audio_reference_tests"
"$project_dir/build-host/audio_reference_tests"
