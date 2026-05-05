# mp_logger

Standalone C logging library for low-latency services and tools.

## Features

- non-blocking log calls backed by a bounded in-memory buffer;
- concurrent producer support with a dedicated worker thread;
- levels: `TRACE`, `DEBUG`, `INFO`, `WARNING`, `ERROR`, `FATAL`;
- default streams: `stdout`, `stderr`, and a per-run local file;
- pluggable stream interface for custom sinks;
- built-in UDP stream support through bootstrap config;
- separate backup file logger for logger-internal warnings and errors;
- CMake build, CTest coverage, installable library packaging, and static plus LLM validation scripts.

## Layout

```text
include/                 public header
src/                     implementation
tests/                   C unit tests
configs/                 bootstrap config template
docs/                    extracted standards and architecture docs
context/                 dedicated retrieval and validation indexes
scripts/                 build, test, deploy, and validator entrypoints
```

## Commands

```bash
./scripts/build.sh
./scripts/test.sh
./scripts/deploy.sh
./scripts/validate-static.sh
./scripts/validate-llm.sh
```

## Bootstrap Config

Template: [configs/logger.bootstrap.ini](configs/logger.bootstrap.ini)

Key controls:

- buffer sizing: `buffer_capacity`, `message_capacity`, `context_capacity`;
- formatting: `format`, `pretty_output`;
- routing: `active_streams`;
- file output: `log_directory`, `file_name_prefix`, `backup_file_name_prefix`;
- network output: `[udp] host`, `port`, and level range.

## Public API

- `mp_logger_bootstrap_load()` loads the bootstrap config file.
- `mp_logger_create()` creates a logger from config without starting the worker thread.
- `mp_logger_add_stream()` registers a custom sink before or after startup.
- `mp_logger_start()` starts the drain worker.
- `mp_logger_log()` enqueues a log entry without blocking on sink I/O.
- `mp_logger_flush()` waits for queued work to drain.
- `mp_logger_shutdown()` stops the worker after draining the queue.

Custom streams receive both the structured record and the already-rendered log line. The logger owns stream teardown only when a `destroy` callback is supplied.
