#!/usr/bin/env bash
set -euo pipefail
source_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${PICLOCATE_BUILD_DIR:-"$source_root/.build/native-release"}
package_dir=${PICLOCATE_PACKAGE_DIR:-"$source_root/dist/PicLocate-$(uname -s)-$(uname -m)"}
cmake -S "$source_root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON "$@"
cmake --build "$build_dir" --parallel "${PICLOCATE_BUILD_JOBS:-4}"
ctest --test-dir "$build_dir" --output-on-failure
cmake --install "$build_dir" --prefix "$package_dir"
cmake --build "$build_dir" --target package
printf 'Ready: %s\n' "$package_dir"
