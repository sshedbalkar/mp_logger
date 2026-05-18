#!/usr/bin/env bash
set -euo pipefail

# Builds mp_logger with tests enabled in a debug CMake build.
#
# Usage examples:
#   ./scripts/build.sh
#   ./scripts/build.sh build/local-debug

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/build.sh [build-dir]

Builds mp_logger with tests enabled in a debug CMake build.

Arguments:
  build-dir
      CMake build directory. Default: build/local-debug.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

build_dir="${1:-build/local-debug}"

cmake -S . -B "$build_dir" -DMP_LOGGER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build "$build_dir"
