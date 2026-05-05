#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

build_dir="${1:-build/release}"
install_dir="${2:-dist/install}"

cmake -S . -B "$build_dir" -DMP_LOGGER_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir"
cmake --install "$build_dir" --prefix "$install_dir"
