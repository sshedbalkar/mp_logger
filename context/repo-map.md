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

## Validation Roots

- `scripts/build.sh`
- `scripts/test.sh`
- `scripts/deploy.sh`
- `scripts/validate-static.sh`
- `scripts/validate-llm.sh`
