#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

git config core.hooksPath .githooks
echo "configured git hooks path: .githooks"
