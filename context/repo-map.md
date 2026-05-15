# Repo Map

## Source Truth

- `AGENTS.md`: bootstrap router, read order, and durable local rules.
- `README.md`: module purpose, commands, public API summary, and structured field behavior.
- `include/mp_logger.h`: public contract, including typed structured field APIs.
- `docs/standards.md`: extracted engineering rules for this logger.
- `docs/architecture.md`: durable architecture decision.
- `docs/commit-messages.md`: local commit subject and body rules.

## Implementation Roots

- `bindings/go/logger.go`: cgo wrapper that exposes logger creation, lifecycle, plain logging, structured-field logging, and stats to Go callers.
- `bindings/go/logger_test.go`: Go coverage for defaults, bootstrap loading, lifecycle, structured fields, and file-backed output.
- `bindings/go/testtmp_test.go`: Go test helper that keeps wrapper temp directories under the parent repo-root `.tmp/` tree.
- `bindings/go/README.md`: Go binding usage, scope, and cgo requirements.
- `src/mp_logger.c`: lifecycle, queueing, structured field storage, worker, and shutdown.
- `src/mp_logger_bootstrap.c`: bootstrap config loading and documented defaults.
- `src/mp_logger_streams.c`: rendering, typed structured field output, file backup logger, and built-in sinks.
- `tests/mp_logger_tests.c`: unit coverage for core behaviors and structured field routing.
- `tests/mp_logger_benchmarks.c`: benchmark harness for file-stream throughput, queue footprint, saturation, and concurrent producer stress.

## Validation Roots

- `scripts/build.sh`
- `scripts/test.sh`
- `scripts/benchmark.sh`
- `scripts/deploy.sh`
- `scripts/install-git-hooks.sh`
- `scripts/validate-commit-message.sh`
- `scripts/validate-static.sh`
- `scripts/validate-llm.sh`
- `.githooks/commit-msg`
