#!/usr/bin/env bash
set -euo pipefail

# Builds and installs a release mp_logger artifact into a local prefix.
#
# Usage examples:
#   ./scripts/deploy.sh
#   ./scripts/deploy.sh build/release dist/install

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/deploy.sh [build-dir] [install-dir]

Builds and installs a release mp_logger artifact into a local prefix.

Arguments:
  build-dir
      CMake build directory. Default: build/release.
  install-dir
      Install prefix. Default: dist/install.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

build_dir="${1:-build/release}"
install_dir="${2:-dist/install}"

cmake -S . -B "$build_dir" -DMP_LOGGER_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir"
cmake --install "$build_dir" --prefix "$install_dir"
