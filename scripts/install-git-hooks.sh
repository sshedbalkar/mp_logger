#!/usr/bin/env bash
set -euo pipefail

# Configures this repository to use the checked-in Git hooks.
#
# Usage examples:
#   ./scripts/install-git-hooks.sh
#   git config --get core.hooksPath

cd "$(dirname "$0")/.."
# shellcheck source=scripts/lib/script-config-env.sh
. ./scripts/lib/script-config-env.sh

if [ "${1:-}" = "--help" ]; then
  cat <<EOF
Usage: ./scripts/install-git-hooks.sh

Configures this repository to use $MP_LOGGER_SCRIPT_DEFAULT_GIT_HOOKS_PATH as the Git hooks path.
EOF
  exit 0
fi

git config core.hooksPath "$MP_LOGGER_SCRIPT_DEFAULT_GIT_HOOKS_PATH"
printf 'configured git hooks path: %s\n' "$MP_LOGGER_SCRIPT_DEFAULT_GIT_HOOKS_PATH"
