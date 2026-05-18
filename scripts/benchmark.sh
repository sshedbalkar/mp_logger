#!/usr/bin/env bash
set -euo pipefail

# Builds and runs mp_logger benchmarks and writes a report.
#
# Usage examples:
#   ./scripts/benchmark.sh
#   ./scripts/benchmark.sh build/local-bench .tmp/reports

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/benchmark.sh [build-dir] [report-dir]

Builds and runs mp_logger benchmarks and writes benchmark-results.md.

Arguments:
  build-dir
      CMake build directory. Default: build/local-bench.
  report-dir
      Benchmark report directory. Default: .tmp/reports.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

build_dir="${1:-build/local-bench}"
report_dir="${2:-.tmp/reports}"
report_path="$report_dir/benchmark-results.md"

mkdir -p "$report_dir"

cmake -S . -B "$build_dir" \
  -DMP_LOGGER_BUILD_TESTS=ON \
  -DMP_LOGGER_BUILD_BENCHMARKS=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir"
"$build_dir/mp_logger_benchmarks" | tee "$report_path"
