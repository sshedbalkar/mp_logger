#!/usr/bin/env bash
set -euo pipefail

# Writes the semantic LLM gap-review checklist after static validation passes.
#
# Usage examples:
#   ./scripts/validate-llm.sh
#   ./scripts/validate-static.sh && ./scripts/validate-llm.sh

if [ "${1:-}" = "--help" ]; then
  cat <<'EOF'
Usage: ./scripts/validate-llm.sh

Writes .tmp/reports/llm-gap-review.md for semantic review.
EOF
  exit 0
fi

cd "$(dirname "$0")/.."

report_dir=".tmp/reports"
report="$report_dir/llm-gap-review.md"

mkdir -p "$report_dir"

cat > "$report" <<'REPORT'
# LLM Gap Review

Use this after `./scripts/validate-static.sh` passes.

- Confirm the queue drop policy is acceptable for the target runtime and hot path.
- Confirm custom stream implementations cannot smuggle secrets or unbounded user content into logs.
- Confirm logger-internal failures only go to the backup logger and never recurse into the primary queue.
- Confirm any new sink keeps business logic outside the logger and owns its own latency budget.
- Confirm docs and config still describe the implemented behavior without hidden extension rules.
REPORT

printf 'llm gap review prompt written to %s\n' "$report"
