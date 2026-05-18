#!/usr/bin/env bash
set -euo pipefail

# Builds mp_logger and runs C plus Go wrapper tests.
#
# Usage examples:
#   ./scripts/test.sh
#   ./scripts/test.sh build/local-debug

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/test.sh [build-dir]

Builds mp_logger and runs C plus Go wrapper tests.

Arguments:
  build-dir
      CMake build directory. Default: build/local-debug.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

build_dir="${1:-build/local-debug}"
repo_tmp_root="$(pwd)/../../.tmp"
temp_dir="$repo_tmp_root/temp"
go_build_dir="$repo_tmp_root/go-build"

mkdir -p "$temp_dir"

./scripts/build.sh "$build_dir"
TMPDIR="$temp_dir" ctest --test-dir "$build_dir" --output-on-failure

if [ -d bindings/go ]; then
  mkdir -p "$go_build_dir"
  (
    cd bindings/go
    TMPDIR="$temp_dir" GOCACHE="$go_build_dir" go test ./...
  )
fi
