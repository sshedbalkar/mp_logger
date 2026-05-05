# AGENTS.md

## Bootstrap

- Read order: `AGENTS.md` -> `README.md` -> `context/routing-map.md` -> `context/repo-map.md` -> route docs/files.
- Choose one route first. Read only route docs and directly touched source files.
- Source truth: `README.md`, `include/mp_logger.h`, `docs/standards.md`, `docs/architecture.md`.
- Validation root: `context/validators/README.md`.

## Routes

- `route.logger-core`: queueing, threading, sinks, public API, config parsing. Read `include/mp_logger.h`, `docs/standards.md`, `docs/architecture.md`, then touched `src/` and `tests/mp_logger_tests.c`.
- `route.docs`: `README.md`, `docs/`, `context/`. Read `docs/standards.md`, `docs/architecture.md`, `context/repo-map.md`.
- `route.validation`: `tests/`, `scripts/`, `CMakeLists.txt`, validators. Read `tests/mp_logger_tests.c`, `scripts/validate-static.sh`, `context/validators/README.md`.

## Durable Rules

- Truth order: source truth > context > chat history.
- `AGENTS.md` and `context/` are routers only; point to owning docs instead of repeating rule bodies.
- If durable navigation or validation facts change, update affected `context/` files in the same change.
- Commit messages must follow `docs/commit-messages.md`.
- Never create a commit in this repository without a subject and body that pass `./scripts/validate-commit-message.sh`.
- Validate doc, context, and script changes with `./scripts/validate-static.sh`.
