#!/usr/bin/env bash
set -euo pipefail

# Builds and runs mp_logger benchmarks and writes a report.
#
# Usage examples:
#   ./scripts/benchmark.sh
#   ./scripts/benchmark.sh build/local-bench .tmp/reports

cd "$(dirname "$0")/.."
# shellcheck source=scripts/lib/script-config-env.sh
. ./scripts/lib/script-config-env.sh

if [ "${1:-}" = "--help" ]; then
  cat <<EOF
Usage: ./scripts/benchmark.sh [build-dir] [report-dir]

Builds and runs mp_logger benchmarks and writes benchmark-results.md.

Arguments:
  build-dir
      CMake build directory. Default: $MP_LOGGER_SCRIPT_DEFAULT_BENCHMARK_BUILD_DIR.
  report-dir
      Benchmark report directory. Default: $MP_LOGGER_SCRIPT_DEFAULT_REPORT_DIR.
EOF
  exit 0
fi

build_dir="${1:-$MP_LOGGER_SCRIPT_DEFAULT_BENCHMARK_BUILD_DIR}"
report_dir="${2:-$MP_LOGGER_SCRIPT_DEFAULT_REPORT_DIR}"
report_path="$(mp_logger_script_join_path "$report_dir" "$MP_LOGGER_SCRIPT_DEFAULT_BENCHMARK_REPORT")"

mkdir -p "$report_dir"

cmake -S . -B "$build_dir" \
  "-DMP_LOGGER_BUILD_TESTS=$MP_LOGGER_SCRIPT_DEFAULT_BUILD_TESTS_ON" \
  "-DMP_LOGGER_BUILD_BENCHMARKS=$MP_LOGGER_SCRIPT_DEFAULT_BUILD_BENCHMARKS_ON" \
  "-DCMAKE_BUILD_TYPE=$MP_LOGGER_SCRIPT_DEFAULT_RELEASE_BUILD_TYPE"
cmake --build "$build_dir"
"$build_dir/mp_logger_benchmarks" | tee "$report_path"
