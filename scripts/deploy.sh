#!/usr/bin/env bash
set -euo pipefail

# Builds and installs a release mp_logger artifact into a local prefix.
#
# Usage examples:
#   ./scripts/deploy.sh
#   ./scripts/deploy.sh build/release dist/install
#   ./scripts/deploy.sh --skip-version-increment

cd "$(dirname "$0")/.."
# shellcheck source=scripts/lib/script-config-env.sh
. ./scripts/lib/script-config-env.sh
# shellcheck source=scripts/lib/version-env.sh
. ./scripts/lib/version-env.sh

if [ "${1:-}" = "--help" ]; then
  cat <<EOF
Usage: ./scripts/deploy.sh [--skip-version-increment] [build-dir] [install-dir]

Builds and installs a release mp_logger artifact into a local prefix.

Options:
  --skip-version-increment
      Use the build_version already present in the build-version config file.

Arguments:
  build-dir
      CMake build directory. Default: $MP_LOGGER_SCRIPT_DEFAULT_RELEASE_BUILD_DIR.
  install-dir
      Install prefix. Default: $MP_LOGGER_SCRIPT_DEFAULT_INSTALL_DIR.
EOF
  exit 0
fi

build_dir=""
install_dir=""
skip_build_version_increment=0

while [ "$#" -gt 0 ]; do
  case "$1" in
    --skip-version-increment|--no-version-increment)
      skip_build_version_increment=1
      ;;
    -*)
      printf 'unknown option: %s\n' "$1" >&2
      printf 'usage: ./scripts/deploy.sh [--skip-version-increment] [build-dir] [install-dir]\n' >&2
      exit 1
      ;;
    *)
      if [ -z "$build_dir" ]; then
      build_dir="$1"
      elif [ -z "$install_dir" ]; then
      install_dir="$1"
      else
      printf 'unexpected argument: %s\n' "$1" >&2
      printf 'usage: ./scripts/deploy.sh [--skip-version-increment] [build-dir] [install-dir]\n' >&2
      exit 1
      fi
      ;;
  esac
  shift
done

build_dir="${build_dir:-$MP_LOGGER_SCRIPT_DEFAULT_RELEASE_BUILD_DIR}"
install_dir="${install_dir:-$MP_LOGGER_SCRIPT_DEFAULT_INSTALL_DIR}"

cmake --fresh -S . -B "$build_dir" \
  "-DMP_LOGGER_BUILD_TESTS=$MP_LOGGER_SCRIPT_DEFAULT_BUILD_TESTS_OFF" \
  "-DCMAKE_BUILD_TYPE=$MP_LOGGER_SCRIPT_DEFAULT_RELEASE_BUILD_TYPE"
cmake --build "$build_dir"
cmake --install "$build_dir" --prefix "$install_dir"

if [ "$skip_build_version_increment" -eq 1 ]; then
  printf 'build version: %s\n' "$(mp_logger_read_build_version_from_config "$MP_LOGGER_BUILD_VERSION_CONFIG_PATH")"
else
  printf 'build version: %s\n' "$(mp_logger_increment_config_build_version "$MP_LOGGER_BUILD_VERSION_CONFIG_PATH")"
fi
