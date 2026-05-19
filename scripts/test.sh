#!/usr/bin/env bash
set -euo pipefail

# Builds mp_logger and runs C plus Go wrapper tests.
#
# Usage examples:
#   ./scripts/test.sh
#   ./scripts/test.sh build/local-debug

cd "$(dirname "$0")/.."
# shellcheck source=scripts/lib/script-config-env.sh
. ./scripts/lib/script-config-env.sh

if [ "${1:-}" = "--help" ]; then
  cat <<EOF
Usage: ./scripts/test.sh [build-dir]

Builds mp_logger and runs C plus Go wrapper tests.

Arguments:
  build-dir
      CMake build directory. Default: $MP_LOGGER_SCRIPT_DEFAULT_BUILD_DIR.
EOF
  exit 0
fi

build_dir="${1:-$MP_LOGGER_SCRIPT_DEFAULT_BUILD_DIR}"
repo_tmp_root="$(mp_logger_script_repo_path "$MP_LOGGER_SCRIPT_DEFAULT_PARENT_TMP_ROOT")"
temp_dir="$(mp_logger_script_join_path "$repo_tmp_root" "$MP_LOGGER_SCRIPT_DEFAULT_TEMP_SUBDIR")"
go_build_dir="$(mp_logger_script_join_path "$repo_tmp_root" "$MP_LOGGER_SCRIPT_DEFAULT_GO_BUILD_SUBDIR")"

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
