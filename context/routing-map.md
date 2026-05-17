# Routing Map

## route.logger-core

- use when: queueing, threading, sink behavior, public API, runtime wrapper, config parsing, or durable constants change
- read first: `include/mp_logger.h`, `include/mp_logger_constants.h`, `docs/standards.md`, `docs/architecture.md`

## route.docs

- use when: README, standards, architecture, or context files change
- read first: `docs/standards.md`, `docs/architecture.md`, `context/repo-map.md`

## route.validation

- use when: tests, build scripts, deploy scripts, or validator scripts change
- read first: `tests/mp_logger_tests.c`, `scripts/validate-static.sh`, `context/validators/README.md`
