# AGENTS.md

## Bootstrap

- Workspace first: `../../../AGENTS.md` -> `../../../README.md` -> `../../../AI_PERSONA.md` -> `../../../docs/README.md` -> `../../../docs/workflow.md` -> relevant `../../../docs/*.md`.
- Read order: `AGENTS.md` -> `README.md` -> `context/routing-map.md` -> `context/repo-map.md` -> route docs/files.
- Choose one route first. Read only route docs and directly touched source files.
- Source truth: `README.md`, `include/mp_logger.h`, `include/mp_logger_constants.h`, `docs/standards.md`, `docs/architecture.md`.
- Validation root: `context/validators/README.md`.

## Routes

- `route.logger-core`: queueing, threading, sinks, public API, config parsing, and durable constants. Read `include/mp_logger.h`, `include/mp_logger_constants.h`, `docs/standards.md`, `docs/architecture.md`, then touched `src/` and `tests/mp_logger_tests.c`.
- `route.docs`: `README.md`, `docs/`, `context/`. Read `docs/standards.md`, `docs/architecture.md`, `context/repo-map.md`.
- `route.validation`: `tests/`, `scripts/`, `CMakeLists.txt`, validators. Read `tests/mp_logger_tests.c`, `scripts/validate-static.sh`, `context/validators/README.md`.

## Durable Rules

- Truth order: source truth > context > chat history.
- `AGENTS.md` and `context/` are routers only; point to owning docs instead of repeating rule bodies.
- Workspace `../../../docs/` is the baseline for common rules; local docs specialize it for the logger library.
- If durable navigation or validation facts change, update affected `context/` files in the same change.
- Commit messages must follow `docs/commit-messages.md`.
- Durable constants, defaults, status names, config keys, stream names, file suffixes, and date formats must be added to `include/mp_logger_constants.h` before implementation, tests, bindings, or scripts consume them.
- New or updated scripts must keep top-level comments with a short purpose statement and a `Usage examples:` block, and must print a `Usage:` block with a successful exit when invoked with `--help`.
- Never create a commit in this repository without a subject and body that pass `./scripts/validate-commit-message.sh`.
- When workspace `../../../docs/` changes, rescan workspace and project docs/context before closeout.
- Validate doc, context, and script changes with `./scripts/validate-static.sh`.
