#!/usr/bin/env bash
set -euo pipefail

# Builds mp_logger with tests enabled in a debug CMake build.
#
# Usage examples:
#   ./scripts/build.sh
#   ./scripts/build.sh build/local-debug
#   ./scripts/build.sh --skip-version-increment

cd "$(dirname "$0")/.."
# shellcheck source=scripts/lib/script-config-env.sh
. ./scripts/lib/script-config-env.sh
# shellcheck source=scripts/lib/version-env.sh
. ./scripts/lib/version-env.sh

if [ "${1:-}" = "--help" ]; then
  cat <<EOF
Usage: ./scripts/build.sh [--skip-version-increment] [build-dir]

Builds mp_logger with tests enabled in a debug CMake build.

Options:
  --skip-version-increment
      Use the build_version already present in the build-version config file.

Arguments:
  build-dir
      CMake build directory. Default: $MP_LOGGER_SCRIPT_DEFAULT_BUILD_DIR.
EOF
  exit 0
fi

build_dir=""
skip_build_version_increment=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --skip-version-increment|--no-version-increment)
      skip_build_version_increment=1
      ;;
    -*)
      printf 'unknown option: %s\n' "$1" >&2
      printf 'usage: ./scripts/build.sh [--skip-version-increment] [build-dir]\n' >&2
      exit 1
      ;;
    *)
      if [ -n "$build_dir" ]; then
      printf 'unexpected argument: %s\n' "$1" >&2
      printf 'usage: ./scripts/build.sh [--skip-version-increment] [build-dir]\n' >&2
      exit 1
      fi
      build_dir="$1"
      ;;
  esac
  shift
done

build_dir="${build_dir:-$MP_LOGGER_SCRIPT_DEFAULT_BUILD_DIR}"

cmake -S . -B "$build_dir" \
  "-DMP_LOGGER_BUILD_TESTS=$MP_LOGGER_SCRIPT_DEFAULT_BUILD_TESTS_ON" \
  "-DCMAKE_BUILD_TYPE=$MP_LOGGER_SCRIPT_DEFAULT_DEBUG_BUILD_TYPE"
cmake --build "$build_dir"

if [ "$skip_build_version_increment" -eq 1 ]; then
  printf 'build version: %s\n' "$(mp_logger_read_build_version_from_config "$MP_LOGGER_BUILD_VERSION_CONFIG_PATH")"
else
  printf 'build version: %s\n' "$(mp_logger_increment_config_build_version "$MP_LOGGER_BUILD_VERSION_CONFIG_PATH")"
fi
