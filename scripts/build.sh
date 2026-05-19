#!/usr/bin/env bash
set -euo pipefail

# Builds mp_logger with tests enabled in a debug CMake build.
#
# Usage examples:
#   ./scripts/build.sh
#   ./scripts/build.sh build/local-debug

cd "$(dirname "$0")/.."
# shellcheck source=scripts/lib/script-config-env.sh
. ./scripts/lib/script-config-env.sh

if [ "${1:-}" = "--help" ]; then
  cat <<EOF
Usage: ./scripts/build.sh [build-dir]

Builds mp_logger with tests enabled in a debug CMake build.

Arguments:
  build-dir
      CMake build directory. Default: $MP_LOGGER_SCRIPT_DEFAULT_BUILD_DIR.
EOF
  exit 0
fi

build_dir="${1:-$MP_LOGGER_SCRIPT_DEFAULT_BUILD_DIR}"

cmake -S . -B "$build_dir" \
  "-DMP_LOGGER_BUILD_TESTS=$MP_LOGGER_SCRIPT_DEFAULT_BUILD_TESTS_ON" \
  "-DCMAKE_BUILD_TYPE=$MP_LOGGER_SCRIPT_DEFAULT_DEBUG_BUILD_TYPE"
cmake --build "$build_dir"
