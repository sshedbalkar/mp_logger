#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

report_dir=".tmp/reports"
report="$report_dir/static-validation.md"
detail="$report_dir/static-validation.tsv"
threshold=85
passed=0
total=0

mkdir -p "$report_dir"
: > "$detail"

# Record one validation row and update the pass counters used for the final score.
record() {
  local status="$1"
  local check_id="$2"
  local description="$3"
  local evidence="$4"
  total=$((total + 1))
  if [ "$status" = "PASS" ]; then
    passed=$((passed + 1))
  fi
  printf '%s\t%s\t%s\t%s\n' "$status" "$check_id" "$description" "$evidence" >> "$detail"
}

# Mark one validation check as passed.
pass() {
  record "PASS" "$1" "$2" "$3"
}

# Mark one validation check as failed.
fail() {
  record "FAIL" "$1" "$2" "$3"
}

# Require a file to exist at a fixed repository path.
require_file() {
  local check_id="$1"
  local path="$2"
  local description="$3"
  if [ -f "$path" ]; then
    pass "$check_id" "$description" "$path"
  else
    fail "$check_id" "$description" "missing: $path"
  fi
}

# Require an exact text snippet so durable docs and rules stay easy to audit.
require_contains() {
  local check_id="$1"
  local path="$2"
  local needle="$3"
  local description="$4"
  if rg -q --fixed-strings -- "$needle" "$path"; then
    pass "$check_id" "$description" "$path contains: $needle"
  else
    fail "$check_id" "$description" "$path missing: $needle"
  fi
}

# Require a function declaration or definition to have an immediately preceding comment block.
require_comment_before() {
  local check_id="$1"
  local path="$2"
  local needle="$3"
  local description="$4"
  if awk -v needle="$needle" '
    { lines[NR] = $0 }
    END {
      for (i = 1; i <= NR; i++) {
        if (index(lines[i], needle) > 0) {
          j = i - 1
          while (j >= 1 && lines[j] ~ /^[[:space:]]*$/) {
            j--
          }
          if (j >= 1 && lines[j] ~ /^[[:space:]]*(\/\/|\/\*|\*|\*\/)/) {
            exit 0
          }
          exit 1
        }
      }
      exit 1
    }
  ' "$path"; then
    pass "$check_id" "$description" "$path matches: $needle"
  else
    fail "$check_id" "$description" "$path missing comment before: $needle"
  fi
}

for path in \
  AGENTS.md \
  .githooks/commit-msg \
  README.md \
  CMakeLists.txt \
  bindings/go/README.md \
  bindings/go/go.mod \
  bindings/go/doc.go \
  bindings/go/logger.go \
  bindings/go/logger_test.go \
  bindings/go/c_mp_logger.c \
  bindings/go/c_mp_logger_bootstrap.c \
  bindings/go/c_mp_logger_streams.c \
  include/mp_logger.h \
  src/mp_logger.c \
  src/mp_logger_bootstrap.c \
  src/mp_logger_streams.c \
  tests/mp_logger_tests.c \
  tests/mp_logger_benchmarks.c \
  configs/logger.bootstrap.ini \
  docs/standards.md \
  docs/architecture.md \
  docs/commit-messages.md \
  context/README.md \
  context/repo-map.md \
  context/doc-cards.md \
  context/routing-map.md \
  context/validators/README.md \
  scripts/benchmark.sh \
  scripts/install-git-hooks.sh \
  scripts/validate-commit-message.sh
do
  require_file "file.${path}" "$path" "required file exists"
done

for script in scripts/build.sh scripts/test.sh scripts/benchmark.sh scripts/deploy.sh scripts/install-git-hooks.sh scripts/validate-commit-message.sh scripts/validate-static.sh scripts/validate-llm.sh .githooks/commit-msg; do
  if [ -x "$script" ]; then
    pass "script.exec.${script}" "script is executable" "$script"
  else
    fail "script.exec.${script}" "script is executable" "$script is not executable"
  fi
done

require_contains "cmake.install" CMakeLists.txt "install(TARGETS mp_logger" "CMake installs the library target"
require_contains "cmake.ctest" CMakeLists.txt "add_test(NAME mp_logger" "CMake registers a CTest target"
require_contains "cmake.bench" CMakeLists.txt "add_executable(mp_logger_benchmarks" "CMake builds the benchmark target"
require_contains "test.go-wrapper" scripts/test.sh "go test ./..." "test script runs Go wrapper tests"
require_contains "agents.read-order" AGENTS.md "Read order:" "AGENTS defines bootstrap read order"
require_contains "agents.commit-format" AGENTS.md "docs/commit-messages.md" "AGENTS routes commit format to local source"
require_contains "agents.commit-validator" AGENTS.md "./scripts/validate-commit-message.sh" "AGENTS requires commit message validation before commit"
require_contains "docs.commit-body" docs/commit-messages.md "The body is mandatory for every commit in this repository." "local commit standard requires commit bodies"
require_contains "hooks.install" README.md "./scripts/install-git-hooks.sh" "README documents git hook installation"
require_contains "hook.commit-msg" .githooks/commit-msg "validate-commit-message.sh" "commit-msg hook calls the commit validator"
require_contains "api.log" include/mp_logger.h "mp_logger_log(" "public header exposes log API"
require_contains "api.level-enabled" include/mp_logger.h "mp_logger_is_level_enabled(" "public header exposes level-enabled API"
require_contains "api.bootstrap" include/mp_logger.h "mp_logger_bootstrap_load(" "public header exposes bootstrap API"
require_contains "bench.script" README.md "./scripts/benchmark.sh" "README documents the benchmark runner"
require_contains "bench.results" README.md "## Benchmark Results" "README records benchmark results"
require_contains "bench.file" README.md "### File stream throughput" "README records file stream benchmark results"
require_contains "readme.go-wrapper" README.md "bindings/go" "README documents the bundled Go wrapper"
require_contains "nonblocking.trylock" src/mp_logger.c "pthread_mutex_trylock" "producer path uses non-blocking queue admission"
require_contains "backup.logger" src/mp_logger_streams.c "mp_logger_backup_write" "backup logger path exists"
require_contains "config.active-streams" configs/logger.bootstrap.ini "active_streams =" "bootstrap config declares active streams"
require_contains "docs.non-blocking" docs/standards.md "non-blocking" "standards doc records non-blocking rule"
require_contains "docs.pluggable" docs/architecture.md "pluggable" "architecture doc records stream extensibility"
require_comment_before "comment.header.defaults" include/mp_logger.h 'void mp_logger_config_init_defaults(' "public header documents config default initialization"
require_comment_before "comment.header.bootstrap" include/mp_logger.h 'mp_log_status_t mp_logger_bootstrap_load(' "public header documents bootstrap loading"
require_comment_before "comment.header.create" include/mp_logger.h 'mp_log_status_t mp_logger_create(' "public header documents logger creation"
require_comment_before "comment.header.create-bootstrap" include/mp_logger.h 'mp_log_status_t mp_logger_create_from_bootstrap(' "public header documents bootstrap-based creation"
require_comment_before "comment.header.add-stream" include/mp_logger.h 'mp_log_status_t mp_logger_add_stream(' "public header documents custom stream registration"
require_comment_before "comment.header.start" include/mp_logger.h 'mp_log_status_t mp_logger_start(' "public header documents worker startup"
require_comment_before "comment.header.log" include/mp_logger.h 'mp_log_status_t mp_logger_log(' "public header documents enqueue semantics"
require_comment_before "comment.header.level-enabled" include/mp_logger.h 'bool mp_logger_is_level_enabled(' "public header documents level-enabled checks"
require_comment_before "comment.header.flush" include/mp_logger.h 'mp_log_status_t mp_logger_flush(' "public header documents flush semantics"
require_comment_before "comment.header.stats" include/mp_logger.h 'mp_log_status_t mp_logger_get_stats(' "public header documents stats retrieval"
require_comment_before "comment.header.shutdown" include/mp_logger.h 'mp_log_status_t mp_logger_shutdown(' "public header documents shutdown semantics"
require_comment_before "comment.header.destroy" include/mp_logger.h 'void mp_logger_destroy(' "public header documents destruction"
require_comment_before "comment.go.default-config" bindings/go/logger.go 'func DefaultConfig(' "Go wrapper documents default config access"
require_comment_before "comment.go.load-bootstrap" bindings/go/logger.go 'func LoadBootstrapConfig(' "Go wrapper documents bootstrap loading"
require_comment_before "comment.go.create" bindings/go/logger.go 'func Create(' "Go wrapper documents logger creation"
require_comment_before "comment.go.create-bootstrap" bindings/go/logger.go 'func CreateFromBootstrap(' "Go wrapper documents bootstrap-based creation"
require_comment_before "comment.go.start" bindings/go/logger.go 'func (logger *Logger) Start(' "Go wrapper documents worker startup"
require_comment_before "comment.go.log" bindings/go/logger.go 'func (logger *Logger) Log(' "Go wrapper documents enqueue semantics"
require_comment_before "comment.go.flush" bindings/go/logger.go 'func (logger *Logger) Flush(' "Go wrapper documents flush semantics"
require_comment_before "comment.go.stats" bindings/go/logger.go 'func (logger *Logger) Stats(' "Go wrapper documents stats retrieval"
require_comment_before "comment.go.shutdown" bindings/go/logger.go 'func (logger *Logger) Shutdown(' "Go wrapper documents shutdown semantics"
require_comment_before "comment.go.close" bindings/go/logger.go 'func (logger *Logger) Close(' "Go wrapper documents teardown semantics"
require_comment_before "comment.core.worker" src/mp_logger.c 'static void *mp_logger_worker_main(' "worker implementation documents its queue-drain approach"
require_comment_before "comment.core.create" src/mp_logger.c 'mp_log_status_t mp_logger_create(' "core creation path documents its allocation approach"
require_comment_before "comment.core.log" src/mp_logger.c 'mp_log_status_t mp_logger_log(' "core enqueue path documents its non-blocking approach"
require_comment_before "comment.core.shutdown" src/mp_logger.c 'mp_log_status_t mp_logger_shutdown(' "core shutdown path documents its join approach"
require_comment_before "comment.bootstrap.load" src/mp_logger_bootstrap.c 'mp_log_status_t mp_logger_bootstrap_load(' "bootstrap parser documents its line-by-line approach"
require_comment_before "comment.streams.render" src/mp_logger_streams.c 'size_t mp_logger_render_record(' "rendering path documents its shared formatting approach"
require_comment_before "comment.streams.builtin" src/mp_logger_streams.c 'mp_log_status_t mp_logger_build_builtin_streams(' "builtin sink setup documents its startup approach"
require_comment_before "comment.tests.find-file" tests/mp_logger_tests.c 'static int find_file_with_prefix(' "test helper documents how it locates run-specific log files"
require_comment_before "comment.tests.overflow" tests/mp_logger_tests.c 'static void test_buffer_saturation_writes_backup_warning(' "overflow test documents its verification approach"
require_comment_before "comment.bench.create" tests/mp_logger_benchmarks.c 'static mp_logger_t *benchmark_create_logger(' "benchmark helper documents synthetic logger setup"
require_comment_before "comment.bench.concurrency" tests/mp_logger_benchmarks.c 'static benchmark_run_result_t benchmark_run_concurrency_case(' "concurrency benchmark documents its measurement approach"
require_comment_before "comment.bench.threshold" tests/mp_logger_benchmarks.c 'static benchmark_run_result_t benchmark_find_threshold_case(' "threshold benchmark documents its search approach"
require_comment_before "comment.bench.file-table" tests/mp_logger_benchmarks.c 'static void benchmark_print_file_stream_table(' "benchmark report documents its file-throughput table"

if rg -n '\b(strcpy|strcat|sprintf|vsprintf|gets)\b' include src tests >/dev/null; then
  fail "c.unsafe-functions" "unsafe C string functions are absent" "unsafe function usage found"
else
  pass "c.unsafe-functions" "unsafe C string functions are absent" "include src tests"
fi

score="$(awk -v passed="$passed" -v total="$total" 'BEGIN { if (total == 0) print "0.0"; else printf "%.1f", (passed / total) * 100 }')"
status="PASS"
if ! awk -v score="$score" -v minimum="$threshold" 'BEGIN { exit (score + 0 >= minimum + 0) ? 0 : 1 }'; then
  status="FAIL"
fi

{
  printf '# Static Validation Report\n\n'
  printf '| Field | Value |\n'
  printf '|:------|:------|\n'
  printf '| Threshold | %s%% |\n' "$threshold"
  printf '| Passed checks | %s |\n' "$passed"
  printf '| Total checks | %s |\n' "$total"
  printf '| Score | %s%% |\n' "$score"
  printf '| Status | %s |\n\n' "$status"
  printf 'Detail: `%s`\n' "$detail"
} > "$report"

printf 'static validation score %s%% (%s/%s)\n' "$score" "$passed" "$total"
if [ "$status" != "PASS" ]; then
  exit 1
fi
