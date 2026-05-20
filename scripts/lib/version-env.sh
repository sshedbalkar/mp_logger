#!/usr/bin/env bash
set -euo pipefail

# Provides mp_logger build-version config parsing, validation, and bump helpers.
#
# Usage examples:
#   . ./scripts/lib/version-env.sh
#   MP_LOGGER_BUILD_VERSION_CONFIG_PATH=configs/logger.bootstrap.yaml . ./scripts/lib/version-env.sh

if [ "${BASH_SOURCE[0]}" = "$0" ] && [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: . ./scripts/lib/version-env.sh

Provides helpers for reading and updating the configured mp_logger build version.

Environment:
  MP_LOGGER_BUILD_VERSION_CONFIG_PATH
      Optional config path override. Default: configs/logger.bootstrap.yaml.
EOF
  exit 0
fi

mp_logger_version_script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MP_LOGGER_REPO_ROOT="${MP_LOGGER_REPO_ROOT:-$(cd "$mp_logger_version_script_dir/../.." && pwd)}"

# shellcheck source=scripts/lib/script-config-env.sh
. "$MP_LOGGER_REPO_ROOT/scripts/lib/script-config-env.sh"

MP_LOGGER_BUILD_VERSION_CONFIG_PATH="${MP_LOGGER_BUILD_VERSION_CONFIG_PATH:-$(mp_logger_script_repo_path "$MP_LOGGER_SCRIPT_DEFAULT_BUILD_VERSION_CONFIG")}"
MP_LOGGER_BUILD_VERSION_KEY="build_version"

mp_logger_version_die() {
  printf 'error: %s\n' "$1" >&2
  exit 1
}

mp_logger_validate_build_version() {
  local build_version_value="$1"

  [[ "$build_version_value" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

mp_logger_require_valid_build_version() {
  local build_version_value="$1"

  mp_logger_validate_build_version "$build_version_value" ||
    mp_logger_version_die "build version must follow MAJOR.MINOR.HOTFIX: $build_version_value"
}

mp_logger_read_build_version_from_config() {
  local version_config_file="${1:-$MP_LOGGER_BUILD_VERSION_CONFIG_PATH}"
  local build_version_value=""

  [ -f "$version_config_file" ] ||
    mp_logger_version_die "build-version config file not found: $version_config_file"

  build_version_value="$(
    awk -F: -v target_key="$MP_LOGGER_BUILD_VERSION_KEY" '
      BEGIN {
        in_root = 1
      }

      function trim(value) {
        gsub(/^[[:space:]]+/, "", value)
        gsub(/[[:space:]]+$/, "", value)
        return value
      }

      /^[[:space:]]*#/ || /^[[:space:]]*$/ {
        next
      }

      /^[^[:space:]][^:]*:[[:space:]]*$/ {
        in_root = 0
        next
      }

      in_root != 1 {
        next
      }

      {
        key = trim($1)
        if (key == target_key) {
          value = trim(substr($0, index($0, ":") + 1))
          gsub(/^"/, "", value)
          gsub(/"$/, "", value)
          print value
          found = 1
          exit
        }
      }

      END {
        if (found != 1) {
          exit 1
        }
      }' "$version_config_file"
  )" || mp_logger_version_die "missing $MP_LOGGER_BUILD_VERSION_KEY in $version_config_file"

  mp_logger_require_valid_build_version "$build_version_value"
  printf '%s\n' "$build_version_value"
}

mp_logger_increment_minor_build_version() {
  local build_version_value="$1"
  local major_version=""
  local minor_version=""
  local ignored_hotfix_version=""

  mp_logger_require_valid_build_version "$build_version_value"
  IFS=. read -r major_version minor_version ignored_hotfix_version <<EOF
$build_version_value
EOF
  printf '%s.%s.0\n' "$major_version" "$((minor_version + 1))"
}

mp_logger_write_build_version_to_config() {
  local next_build_version="$1"
  local version_config_file="${2:-$MP_LOGGER_BUILD_VERSION_CONFIG_PATH}"
  local version_config_directory=""
  local version_config_temp_file=""

  mp_logger_require_valid_build_version "$next_build_version"
  [ -f "$version_config_file" ] ||
    mp_logger_version_die "build-version config file not found: $version_config_file"

  version_config_directory="$(dirname "$version_config_file")"
  version_config_temp_file="$(mktemp "$version_config_directory/.build-version.XXXXXX")"

  awk -v target_key="$MP_LOGGER_BUILD_VERSION_KEY" -v next_value="$next_build_version" '
    BEGIN {
      in_root = 1
    }

    /^[^[:space:]][^:]*:[[:space:]]*$/ {
      if (wrote != 1) {
        print target_key ": \"" next_value "\""
        wrote = 1
      }
      print
      in_root = 0
      next
    }

    in_root != 1 {
      print
      next
    }

    {
      line = $0
      split(line, parts, ":")
      key = parts[1]
      gsub(/^[[:space:]]+/, "", key)
      gsub(/[[:space:]]+$/, "", key)
      if (key == target_key) {
        print target_key ": \"" next_value "\""
        wrote = 1
        next
      }
      print
    }

    END {
      if (wrote != 1) {
        print target_key ": \"" next_value "\""
      }
    }' "$version_config_file" >"$version_config_temp_file"

  mv "$version_config_temp_file" "$version_config_file"
}

mp_logger_increment_config_build_version() {
  local version_config_file="${1:-$MP_LOGGER_BUILD_VERSION_CONFIG_PATH}"
  local current_build_version=""
  local next_build_version=""

  current_build_version="$(mp_logger_read_build_version_from_config "$version_config_file")"
  next_build_version="$(mp_logger_increment_minor_build_version "$current_build_version")"
  mp_logger_write_build_version_to_config "$next_build_version" "$version_config_file"
  printf '%s\n' "$next_build_version"
}
