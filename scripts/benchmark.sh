#!/usr/bin/env bash
set -euo pipefail

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
