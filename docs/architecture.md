# Architecture: mp_logger

## Status

Accepted

## Context

The host project needs a reusable logging foundation that stays outside Go-specific application code, keeps producer calls off sink I/O, supports multiple outputs, and can later live as an independent repository.

## Decision

`mp_logger` is implemented as a standalone C library with:

- a bounded in-memory queue for producer calls;
- a dedicated worker thread that renders and fans out records to streams;
- built-in `stdout`, `stderr`, `file`, and `udp` sinks selected by bootstrap config;
- a pluggable public callback interface for additional sinks;
- a separate backup file logger for logger-internal warnings and errors.

## Alternatives Considered

- synchronous writes on every log call: rejected because it blocks hot paths;
- app-specific Go logging only: rejected because the module must stay stack-agnostic;
- lock-free multi-sink transport: rejected for now because the added complexity was not justified for an early-build reusable foundation.

## Data Flow

1. Producer calls `mp_logger_log(level, message, context)`.
2. The logger attempts a non-blocking queue lock.
3. On success, the record is copied into a preallocated slot.
4. The worker thread dequeues, renders, and writes the record to active sinks.
5. Sink failures and drop counters are mirrored to the backup log file.

## Consequences

- Producer calls avoid sink I/O latency and remain bounded.
- Queue contention or saturation can drop records; that tradeoff is explicit and observable.
- The library stays deployable into other runtimes through a plain C API.
- Custom sinks can still block the worker thread, so extensions must own their own latency budget.

## Validation

- `ctest` runs the unit suite for config, queue pressure, custom streams, and file output.
- `scripts/validate-static.sh` enforces file layout, install rules, unsafe-function bans, and required docs.
- `scripts/validate-llm.sh` emits the semantic review checklist.

## Rollout And Rollback

- Roll out first as a standalone library and parent-repo native module.
- Integrate into host runtimes only through narrow adapters.
- Roll back by removing the adapter or submodule reference; the library is isolated from business-state code.
