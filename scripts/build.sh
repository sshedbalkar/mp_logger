#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

build_dir="${1:-build/local-debug}"

cmake -S . -B "$build_dir" -DMP_LOGGER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir"
