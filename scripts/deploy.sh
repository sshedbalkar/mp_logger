#!/usr/bin/env bash
set -euo pipefail

# Builds and installs a release mp_logger artifact into a local prefix.
#
# Usage examples:
#   ./scripts/deploy.sh
#   ./scripts/deploy.sh build/release dist/install

cd "$(dirname "$0")/.."
# shellcheck source=scripts/lib/script-config-env.sh
. ./scripts/lib/script-config-env.sh

if [ "${1:-}" = "--help" ]; then
  cat <<EOF
Usage: ./scripts/deploy.sh [build-dir] [install-dir]

Builds and installs a release mp_logger artifact into a local prefix.

Arguments:
  build-dir
      CMake build directory. Default: $MP_LOGGER_SCRIPT_DEFAULT_RELEASE_BUILD_DIR.
  install-dir
      Install prefix. Default: $MP_LOGGER_SCRIPT_DEFAULT_INSTALL_DIR.
EOF
  exit 0
fi

build_dir="${1:-$MP_LOGGER_SCRIPT_DEFAULT_RELEASE_BUILD_DIR}"
install_dir="${2:-$MP_LOGGER_SCRIPT_DEFAULT_INSTALL_DIR}"

cmake -S . -B "$build_dir" \
  "-DMP_LOGGER_BUILD_TESTS=$MP_LOGGER_SCRIPT_DEFAULT_BUILD_TESTS_OFF" \
  "-DCMAKE_BUILD_TYPE=$MP_LOGGER_SCRIPT_DEFAULT_RELEASE_BUILD_TYPE"
cmake --build "$build_dir"
cmake --install "$build_dir" --prefix "$install_dir"
