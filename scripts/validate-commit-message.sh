#!/usr/bin/env bash
set -euo pipefail

# Validates one commit message file against the local commit standard.
#
# Usage examples:
#   ./scripts/validate-commit-message.sh .git/COMMIT_EDITMSG
#   ./scripts/validate-commit-message.sh .tmp/commit-message.txt

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/validate-commit-message.sh <commit-message-file>

Validates one commit message file against docs/commit-messages.md.

Arguments:
  commit-message-file
      Path to the commit message file to validate.
EOF
  exit 0
fi

if [ "$#" -ne 1 ]; then
  echo "Usage: $0 <commit-message-file>" >&2
  exit 1
fi

message_file="$1"

if [ ! -f "$message_file" ]; then
  echo "commit message file not found: $message_file" >&2
  exit 1
fi

mapfile -t lines < "$message_file"

subject="${lines[0]-}"
separator="${lines[1]-}"
body_found=0
index=0

if [ -z "$subject" ]; then
  echo "commit subject is required" >&2
  exit 1
fi

if ! [[ "$subject" =~ ^(feat|fix|refactor|test|docs|build|perf|security|db|ops|style|ci|chore|revert|hotfix|infra)\([a-z0-9-]+\):\ .+ ]]; then
  echo "commit subject must match <type>(<scope>): <summary>" >&2
  exit 1
fi

if [ "${#lines[@]}" -lt 3 ]; then
  echo "commit body is required" >&2
  exit 1
fi

if [ -n "$separator" ]; then
  echo "commit subject must be followed by a blank separator line" >&2
  exit 1
fi

for ((index = 2; index < ${#lines[@]}; index++)); do
  if [ -n "${lines[$index]}" ]; then
    body_found=1
    break
  fi
done

if [ "$body_found" -ne 1 ]; then
  echo "commit body is required" >&2
  exit 1
fi
