# Repo Map

## Source Truth

- `AGENTS.md`: bootstrap router, read order, and durable local rules.
- `README.md`: module purpose, commands, and public API summary.
- `include/mp_logger.h`: public contract.
- `docs/standards.md`: extracted engineering rules for this logger.
- `docs/architecture.md`: durable architecture decision.

## Implementation Roots

- `src/mp_logger.c`: lifecycle, queueing, worker, and shutdown.
- `src/mp_logger_bootstrap.c`: bootstrap config loading.
- `src/mp_logger_streams.c`: rendering, file backup logger, and built-in sinks.
- `tests/mp_logger_tests.c`: unit coverage for core behaviors.
- `tests/mp_logger_benchmarks.c`: benchmark harness for file-stream throughput, queue footprint, saturation, and concurrent producer stress.

## Validation Roots

- `scripts/build.sh`
- `scripts/test.sh`
- `scripts/benchmark.sh`
- `scripts/deploy.sh`
- `scripts/validate-static.sh`
- `scripts/validate-llm.sh`
