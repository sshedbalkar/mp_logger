#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

build_dir="${1:-build/local-debug}"

./scripts/build.sh "$build_dir"
ctest --test-dir "$build_dir" --output-on-failure
