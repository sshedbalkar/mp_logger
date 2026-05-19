#!/usr/bin/env bash
set -euo pipefail

# Loads centralized mp_logger shell-script defaults and path helpers.
#
# Usage examples:
#   . ./scripts/lib/script-config-env.sh
#   MP_LOGGER_SCRIPT_CONFIG_FILE=configs/scripts/defaults.env . ./scripts/lib/script-config-env.sh

if [ "${BASH_SOURCE[0]}" = "$0" ] && [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: . ./scripts/lib/script-config-env.sh

Loads centralized mp_logger shell-script defaults and path helpers.

Environment:
  MP_LOGGER_SCRIPT_CONFIG_FILE
      Optional script defaults file override. Default: configs/scripts/defaults.env.
EOF
  exit 0
fi

mp_logger_script_config_detect_repo_root() {
  local script_config_dir
  script_config_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  cd "$script_config_dir/../.." && pwd
}

MP_LOGGER_REPO_ROOT="${MP_LOGGER_REPO_ROOT:-$(mp_logger_script_config_detect_repo_root)}"
MP_LOGGER_SCRIPT_CONFIG_FILE="${MP_LOGGER_SCRIPT_CONFIG_FILE:-$MP_LOGGER_REPO_ROOT/configs/scripts/defaults.env}"

[ -f "$MP_LOGGER_SCRIPT_CONFIG_FILE" ] || {
  printf 'error: missing mp_logger script defaults config: %s\n' "$MP_LOGGER_SCRIPT_CONFIG_FILE" >&2
  exit 1
}

# shellcheck disable=SC1090
. "$MP_LOGGER_SCRIPT_CONFIG_FILE"

mp_logger_script_repo_path() {
  local configured_path="$1"

  case "$configured_path" in
    /*)
      printf '%s\n' "$configured_path"
      ;;
    *)
      printf '%s/%s\n' "$MP_LOGGER_REPO_ROOT" "$configured_path"
      ;;
  esac
}

mp_logger_script_join_path() {
  local parent_path="$1"
  local child_path="$2"

  case "$parent_path" in
    */)
      printf '%s%s\n' "$parent_path" "$child_path"
      ;;
    *)
      printf '%s/%s\n' "$parent_path" "$child_path"
      ;;
  esac
}
