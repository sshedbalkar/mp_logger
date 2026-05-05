# mp_logger Standards

## Scope

These rules describe the implementation-backed standards for the standalone logger library, its public C API, its built-in sinks, and its repo-local validation flow.

## Runtime Model

```text
producer thread(s)
    |
    | mp_logger_log()
    v
trylock queue mutex
    |
    +--> BUSY / QUEUE_FULL -> stats counters -> backup warning path
    |
    v
preallocated queue slots
    |
    v
worker thread
    |
    +--> render once (text/json)
    |
    +--> builtin streams
    |
    `--> custom streams
```

## Public API Rules

- Keep the public contract small, explicit, and owned by `include/mp_logger.h`.
- Keep `mp_logger_create()` responsible for validation and resource acquisition only; it must not leak partially initialized state.
- Keep `mp_logger_create_from_bootstrap()` all-or-nothing: load config, create logger, start worker, or fail without returning a usable handle.
- Keep `mp_logger_start()` idempotent for already running loggers.
- Keep `mp_logger_start()` rejecting zero-stream loggers so the worker never starts without a sink.
- Keep `mp_logger_destroy()` safe for stopped and still-running loggers.

## Concurrency And Memory Rules

- Keep producer-side logging non-blocking with `pthread_mutex_trylock()` on the queue path.
- Keep producer-side logging free of sink I/O and heap allocation after logger creation.
- Keep queue storage bounded and preallocated from `buffer_capacity`, `message_capacity`, and `context_capacity`.
- Keep the worker copying queued message and context text into thread-local scratch buffers before invoking stream callbacks.
- Keep drop behavior explicit: contention returns `MP_LOG_STATUS_BUSY`, saturation returns `MP_LOG_STATUS_QUEUE_FULL`.

## Configuration Rules

- Keep `mp_logger_config_init_defaults()` and the documented defaults in `README.md` aligned.
- Keep bootstrap config file-backed and explicit; reject unknown sections, unknown keys, malformed lines, and malformed values with `MP_LOG_STATUS_CONFIG_ERROR`.
- Keep supported bootstrap sections limited to root keys plus `[logger]`, `[stdout]`, `[stderr]`, `[file]`, and `[udp]`.
- Keep `active_streams` comma-separated, order-preserving, and trim surrounding ASCII whitespace; an empty value disables built-in streams.
- Keep built-in stream names limited to `stdout`, `stderr`, `file`, and `udp`.
- Keep `log_directory` defaulting to `.` so primary and backup files land in the current working directory unless overridden.
- Keep file name prefixes sanitized to ASCII letters, digits, `_`, and `-` before path construction.

## Rendering And Sink Rules

- Keep rendering centralized on the worker thread so every sink sees the same formatted entry.
- Keep every rendered record carrying timestamp, level, service, environment, sequence ID, message, and optional context.
- Keep timestamps in UTC with millisecond precision.
- Keep text and JSON output single-line and escaped so control characters cannot forge extra records.
- Keep `pretty_output` limited to JSON spacing changes; it must not change the field set or switch to multi-line output.
- Keep stream callbacks receiving both the structured record and the already rendered entry.
- Keep stream names unique within a logger instance.

## Backup Logger Rules

- Keep the backup logger separate from the main queue and open it before built-in stream initialization.
- Keep logger-internal warnings and errors routed to the backup file logger instead of back into the primary queue.
- Keep backup writes serialized so producer-side and worker-side warning paths cannot interleave.
- Keep built-in stream initialization failures, stream write failures, and observed drop counters visible through the backup logger.

## Observability Rules

- Keep `mp_logger_get_stats()` as the supported way to observe queued, processed, `dropped_busy`, `dropped_full`, and active stream counts.
- Keep queue pressure observable both through stats counters and backup warning lines.
- Keep custom stream latency off the producer path, but document that slow custom streams still block the single worker thread.

## Validation Rules

- Keep unit tests covering bootstrap overrides, pre-start saturation, custom stream rendering, file output, backup warning emission, and stream-limit enforcement.
- Keep benchmark coverage documenting queue contention and saturation behavior alongside throughput measurements.
- Keep static validation checking required docs, install rules, config templates, public API presence, and unsafe C function absence.
- Keep doc, context, and script changes validated with `./scripts/validate-static.sh`.
