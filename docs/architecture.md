# Architecture: mp_logger

## Status

Accepted

## Context

The repository needs a reusable logging foundation that stays outside application business logic, exposes a plain C API, supports a narrow Go adapter, and keeps producer calls off sink I/O. The implementation must stay bounded in memory, explicit in failure modes, and easy to embed in services or tools that want file, stdio, UDP, or custom callback sinks.

## Decision

`mp_logger` is implemented as a standalone C library with:

- a validated config object that can be built in code or loaded from a bootstrap INI file;
- a bounded queue backed by preallocated message and context buffers;
- producer calls that use `pthread_mutex_trylock()` and return `BUSY` or `QUEUE_FULL` instead of blocking;
- one worker thread that dequeues records, renders them once, and fans them out to every active sink;
- built-in `stdout`, `stderr`, `file`, and `udp` sinks selected from `config.active_streams`;
- a public callback interface for custom sinks;
- a separate backup file logger for logger-internal warnings and errors.

## Runtime Layout

```text
                 +----------------------+
                 |   mp_logger_config   |
                 | code or bootstrap    |
                 +----------+-----------+
                            |
                            v
                 +----------------------+
                 |   mp_logger_create   |
                 | validate config      |
                 | sanitize prefixes    |
                 | alloc queue storage  |
                 | open backup file     |
                 | build builtin sinks  |
                 +----------+-----------+
                            |
          add custom sinks  |  mp_logger_start()
                before/after|         |
                            v         v
                      +----------------------+
                      |      mp_logger       |
                      | queue + worker       |
                      | stream registry      |
                      | backup logger        |
                      +----------------------+
```

## Data Flow

```text
producer thread(s)
    |
    | mp_logger_log(level, message, context)
    v
trylock(queue_mutex)
    |
    +--> lock busy -----------> dropped_busy++ ----------+
    |                                                    |
    +--> queue full ----------> dropped_full++ ----------+--> backup warnings
    |
    `--> copy into queue slot -> signal worker ----------+

worker thread
    |
    +--> dequeue into local scratch buffers
    +--> render once (text/json)
    `--> fan out to matching streams in registration order
             |
             `--> stream failure -> backup error line
```

## Lifecycle Semantics

```text
config_init_defaults()
        |
        +--> optional bootstrap overrides
        |
create()
        |
        +--> builtin streams may partially fail to initialize
        |    failure is mirrored to backup logger
        |
        +--> no streams is still a valid created state
        |
start()
        |
        +--> requires at least one registered stream
        |
log() / flush()
        |
shutdown()
        |
destroy()
```

- `mp_logger_create_from_bootstrap()` is the all-in-one path for load, create, and start.
- `mp_logger_create()` is the customization path for callers that want to register custom sinks before startup.
- Built-in stream activation follows the order declared in `active_streams`.
- The backup logger is opened before built-in streams so startup failures still have a durable warning path.

## Alternatives Considered

- synchronous writes on every log call: rejected because sink latency would leak into producer hot paths;
- app-specific Go logging only: rejected because the logger is intended to stay runtime-agnostic;
- lock-free multi-sink transport: rejected because the added complexity is not justified for the current bounded, single-worker design.

## Consequences

- Producer calls stay bounded and avoid sink I/O latency.
- Queue contention and saturation are explicit parts of the API surface and are observable through stats and backup warnings.
- Rendering is centralized, so built-in and custom sinks receive the same escaped, single-line output.
- Built-in sink initialization can degrade partially; a logger may be created successfully but still fail `start()` if no streams end up active.
- Slow custom sinks can still stall the worker thread, so sink extensions own their latency budget.

## Validation

- `tests/mp_logger_tests.c` covers bootstrap overrides, queue saturation before and after startup, custom stream rendering, file-backed output, backup warning emission, and stream-limit enforcement.
- `tests/mp_logger_benchmarks.c` measures file-stream throughput, queue pressure, and concurrent producer behavior while documenting the `BUSY` and `QUEUE_FULL` drop paths.
- `scripts/validate-static.sh` enforces required docs, API presence, install rules, config templates, and unsafe-function bans.
- `scripts/validate-llm.sh` remains the semantic review layer for drop policy, log hygiene, and extension safety.

## Rollout And Rollback

- Roll out through the C API first, then keep language-specific adapters narrow and disposable.
- Roll back by removing the embedding adapter; the logger remains isolated from application business state.
