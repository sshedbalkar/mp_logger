#!/usr/bin/env bash
set -euo pipefail

# Configures this repository to use the checked-in Git hooks.
#
# Usage examples:
#   ./scripts/install-git-hooks.sh
#   git config --get core.hooksPath

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/install-git-hooks.sh

Configures this repository to use .githooks as the Git hooks path.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

git config core.hooksPath .githooks
echo "configured git hooks path: .githooks"
