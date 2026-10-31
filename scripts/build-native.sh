#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cygpath -u "$AIPHONE_PROJECT_DIR")"
cache_dir="$(cygpath -u "$AIPHONE_BUILD_ROOT")"
bash "$project_dir/scripts/build-pjsip.sh"
export PKG_CONFIG_PATH="$cache_dir/pj-install/lib/pkgconfig"
cmake -S "$project_dir" -B "$project_dir/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DAIPHONE_DEPS="$(cygpath -m "$cache_dir")"
cmake --build "$project_dir/build" --parallel 6
if [[ "${AIPHONE_SKIP_TESTS:-0}" != 1 ]]; then
    ctest --test-dir "$project_dir/build" --output-on-failure
fi
